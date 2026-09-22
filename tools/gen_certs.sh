#!/usr/bin/env bash
#
# gen_certs.sh — project CA + per-device TLS server certificate.
#
# Why a CA and not a self-signed leaf: Safari refuses fetch() to an HTTPS
# endpoint with an untrusted certificate and gives the webapp no "continue
# anyway" affordance, so the old self-signed cert simply cannot work from a
# browser. The fix is one project CA installed on the iPad once (as a
# .mobileconfig profile or through MDM, then trusted under
# Settings > General > About > Certificate Trust Settings), with each device
# holding a leaf certificate that CA signed.
#
# The SAN is the mDNS name, NOT an IP: the address comes from DHCP and can
# move, and a certificate pinned to an address that changes is a certificate
# that stops working on lease renewal.
#
# Usage:
#   ./gen_certs.sh <device-hostname>     e.g. ./gen_certs.sh ste-d7eac3
#
# The CA key is generated once and then REUSED. Keep ca/ca_key.pem offline:
# anyone holding it can mint a certificate any provisioned iPad will trust.
set -euo pipefail

HOST="${1:-}"
if [[ -z "$HOST" ]]; then
    echo "usage: $0 <device-hostname>   (e.g. ste-d7eac3)" >&2
    exit 1
fi

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CA_DIR="$HERE/ca"
OUT_DIR="$HERE/../main/certs"
mkdir -p "$CA_DIR" "$OUT_DIR"

# ── Project CA (created once, reused for every device) ───────────────────────
if [[ ! -f "$CA_DIR/ca_key.pem" ]]; then
    echo "creating project CA (once) — keep ca/ca_key.pem offline"
    openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
        -keyout "$CA_DIR/ca_key.pem" -out "$CA_DIR/ca_cert.pem" \
        -subj "/C=VN/O=ETeams/CN=ETeams STE Project CA" \
        -addext "basicConstraints=critical,CA:TRUE,pathlen:0" \
        -addext "keyUsage=critical,keyCertSign,cRLSign"
fi

# ── Device leaf ──────────────────────────────────────────────────────────────
openssl req -newkey rsa:2048 -nodes \
    -keyout "$OUT_DIR/tls_server_key.pem" \
    -out "$CA_DIR/$HOST.csr" \
    -subj "/C=VN/O=ETeams/CN=$HOST.local"

cat > "$CA_DIR/$HOST.ext" <<EXT
basicConstraints=CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=DNS:$HOST.local,DNS:$HOST
EXT

openssl x509 -req -in "$CA_DIR/$HOST.csr" \
    -CA "$CA_DIR/ca_cert.pem" -CAkey "$CA_DIR/ca_key.pem" -CAcreateserial \
    -out "$OUT_DIR/tls_server_cert.pem" -days 3650 -sha256 \
    -extfile "$CA_DIR/$HOST.ext"

rm -f "$CA_DIR/$HOST.csr" "$CA_DIR/$HOST.ext"

echo
echo "device certificate written to main/certs/ (embedded into the .bin at build time)"
echo "install $CA_DIR/ca_cert.pem on the iPad once, and trust it under"
echo "Settings > General > About > Certificate Trust Settings"
