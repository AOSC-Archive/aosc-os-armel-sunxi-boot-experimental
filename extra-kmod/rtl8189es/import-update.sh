#!/bin/sh

die() {
    echo $@ >&2
    exit 255
}

IMPORT_DIR="$1"
test -d "${IMPORT_DIR}" || die "Missing import dir ${IMPORT_DIR}"
test -f "${IMPORT_DIR}/Kconfig" -a -f "${IMPORT_DIR}/Kconfig" || die "Missing Kconfig and Makefile in import dir ${IMPORT_DIR}"
IMPORT_BASE=`basename "${IMPORT_DIR}"`

git checkout import
rm -rf *
(cd "${IMPORT_DIR}" && tar cf -) | tar xf -
git rm $(git ls-files --deleted)
git add .
git commit -s "Importing ${IMPORT_BASE}"
git checkout master
git rebase import
