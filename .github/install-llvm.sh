#!/bin/bash
# Installs LLVM ${LLVM_MAJOR} from apt.llvm.org plus the given apt packages on the Ubuntu 24.04 runners
# (ci.yml). apt.llvm.org was unreachable several times on 2026-11-03: the whole step is retried up
# to three times, and a failure prints what the runner saw.
#   .github/install-llvm.sh <package>...
set -uo pipefail
: "${LLVM_MAJOR:?}"
attempt() {
    curl -fsSL --retry 5 --retry-delay 10 --retry-all-errors https://apt.llvm.org/llvm-snapshot.gpg.key \
        | sudo gpg --batch --yes --dearmor -o /usr/share/keyrings/llvm-snapshot.gpg || return 1
    echo "deb [signed-by=/usr/share/keyrings/llvm-snapshot.gpg] https://apt.llvm.org/noble/ llvm-toolchain-noble-${LLVM_MAJOR} main" \
        | sudo tee /etc/apt/sources.list.d/llvm.list >/dev/null
    sudo apt-get -o Acquire::Retries=5 update || return 1
    sudo apt-get -o Acquire::Retries=5 install -y --no-install-recommends "$@" || return 1
}
for try in 1 2 3; do
    if attempt "$@"; then
        exit 0
    fi
    echo "::warning::LLVM installation attempt ${try} failed"
    getent hosts apt.llvm.org || echo "apt.llvm.org does not resolve"
    curl -sS -o /dev/null -w 'apt.llvm.org key: HTTP %{http_code}\n' --max-time 20 \
        https://apt.llvm.org/llvm-snapshot.gpg.key || true
    sleep $((try * 30))
done
echo "::error::LLVM ${LLVM_MAJOR} could not be installed from apt.llvm.org"
exit 1
