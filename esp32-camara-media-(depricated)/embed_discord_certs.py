#
# PlatformIO extra_script: Embed Discord SSL certificates
#
# The esp-discord library uses ESP-IDF's EMBED_TXTFILES to embed PEM
# certificates as binary symbols (_binary_api_pem_start, _binary_gateway_pem_start).
# PlatformIO's Arduino framework does not process EMBED_TXTFILES, so this
# script replicates that behavior using objcopy from the toolchain.
#
# Certificates are generated once (via openssl) and cached in the cert/ directory.
# Delete cert/ to force regeneration.
#

Import("env")
import os
import subprocess
import sys
import shutil

HOSTS = [
    ("gateway.discord.gg", "gateway.pem"),
    ("discord.com", "api.pem"),
]


def generate_cert_openssl(hostname, output_path):
    """Generate PEM root CA certificate using openssl CLI."""
    openssl = shutil.which("openssl")
    if not openssl:
        return False

    try:
        proc = subprocess.run(
            [openssl, "s_client", "-showcerts", "-connect", "%s:443" % hostname],
            input=b"",
            capture_output=True,
            timeout=15,
        )
        output = proc.stdout.decode("utf-8", errors="replace")

        # Extract all certificates from the chain
        certs = []
        in_cert = False
        current_cert = []
        for line in output.split("\n"):
            if "BEGIN CERTIFICATE" in line:
                in_cert = True
                current_cert = [line]
            elif "END CERTIFICATE" in line:
                current_cert.append(line)
                certs.append("\n".join(current_cert))
                in_cert = False
            elif in_cert:
                current_cert.append(line)

        if certs:
            # Use the last certificate in the chain (root CA)
            with open(output_path, "wb") as f:
                f.write(certs[-1].encode("utf-8"))
                f.write(b"\n")
                # Null-terminate (EMBED_TXTFILES adds a null terminator)
                f.write(b"\x00")
            return True
    except (subprocess.TimeoutExpired, FileNotFoundError, OSError) as e:
        print("  openssl failed for %s: %s" % (hostname, e))

    return False


def generate_cert_python(hostname, output_path):
    """Fallback: get server certificate using Python ssl module."""
    import ssl

    try:
        pem = ssl.get_server_certificate((hostname, 443))
        with open(output_path, "wb") as f:
            f.write(pem.encode("utf-8"))
            # Null-terminate (EMBED_TXTFILES adds a null terminator)
            f.write(b"\x00")
        return True
    except Exception as e:
        print("  Python ssl failed for %s: %s" % (hostname, e))
        return False


def embed_discord_certs(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")
    build_dir = env.subst("$BUILD_DIR")
    cert_dir = os.path.join(project_dir, "cert")

    os.makedirs(cert_dir, exist_ok=True)
    os.makedirs(build_dir, exist_ok=True)

    # Step 1: Generate certificates if they don't exist
    for hostname, filename in HOSTS:
        cert_path = os.path.join(cert_dir, filename)
        if not os.path.exists(cert_path):
            print("Generating Discord certificate for %s..." % hostname)
            if not generate_cert_openssl(hostname, cert_path):
                if not generate_cert_python(hostname, cert_path):
                    raise RuntimeError(
                        "Failed to generate certificate for %s.\n"
                        "Please install openssl or run certgen.sh manually:\n"
                        "  cd %s && bash certgen.sh\n"
                        "Then append a null byte to each .pem file."
                        % (hostname, project_dir)
                    )
            print("  -> %s" % cert_path)

    # Step 2: Find objcopy from toolchain
    cc_path = env.subst("$CC")
    cc_dir = os.path.dirname(cc_path)
    prefix = "xtensa-esp32-elf-"

    objcopy = shutil.which(prefix + "objcopy", path=cc_dir)
    if not objcopy:
        objcopy = shutil.which(prefix + "objcopy")
    if not objcopy:
        # Try replacing gcc with objcopy in the CC path
        objcopy = cc_path.replace("gcc", "objcopy").replace("cc", "objcopy")

    # Step 3: Convert PEM files to linkable object files
    for _, filename in HOSTS:
        cert_path = os.path.join(cert_dir, filename)
        obj_path = os.path.join(build_dir, "cert_" + filename.replace(".", "_") + ".o")

        # objcopy derives symbol names from the input filename:
        #   gateway.pem -> _binary_gateway_pem_start, _binary_gateway_pem_end
        #   api.pem     -> _binary_api_pem_start, _binary_api_pem_end
        # We use cwd=cert_dir so only the filename (not full path) is used.
        print("Embedding %s -> %s" % (filename, obj_path))
        subprocess.check_call(
            [
                objcopy,
                "--input-target", "binary",
                "--output-target", "elf32-xtensa-le",
                "--binary-architecture", "xtensa",
                filename,
                obj_path,
            ],
            cwd=cert_dir,
        )

        env.Append(LINKFLAGS=[obj_path])


env.AddPreAction("$BUILD_DIR/${PROGNAME}.elf", embed_discord_certs)
