#!/usr/bin/env bash
set -euo pipefail
test_dir=$(cd -- "$(dirname -- "$0")" && pwd)
build_dir=$(realpath "${1:-$test_dir/../build}")
smoke_root=$(mktemp -d "$build_dir/smoke-XXXXXXXX")
mkdir -p "$smoke_root/plug-ins/file-tsre-ace" "$smoke_root/profile"
binary=file-tsre-ace
if [[ ${2:-} == --dialog ]]; then
    binary=file-tsre-ace-dialog-test
    export ACE_TEST_DIALOG=1
fi
cp "$build_dir/$binary" "$smoke_root/plug-ins/file-tsre-ace/file-tsre-ace"
system_plugins="$(pkg-config --variable=gimplibdir gimp-3.0)/plug-ins"
printf '(plug-in-path "%s:%s")\n' "$smoke_root/plug-ins" "$system_plugins" > "$smoke_root/gimprc"
export GIMP3_DIRECTORY="$smoke_root/profile"
export ACE_TEST_OUTPUT="$smoke_root/output"
: > "$GIMP3_DIRECTORY/theme.css"
encoded=$(base64 -w0 "$test_dir/gimp-smoke.py")
gimp --new-instance --no-interface --no-data --no-fonts --no-splash --console-messages \
    --gimprc "$smoke_root/gimprc" --batch-interpreter python-fu-eval \
    --batch "import base64; exec(base64.b64decode('$encoded'))" --quit
test -f "$ACE_TEST_OUTPUT/PASS"
files=()
for path in "$ACE_TEST_OUTPUT/"*.ace; do
    [[ $path == */must-survive.ace ]] || files+=("$path")
done
"$build_dir/ace_export_tests" "${files[@]}"
printf 'Integration results: %s\n' "$smoke_root"
