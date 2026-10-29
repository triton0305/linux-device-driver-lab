SUMMARY = "Chapter 06B thermal_core character driver userspace test"
LICENSE = "CLOSED"
SRC_URI = "file://thermal_test.c"
S = "${UNPACKDIR}"

do_compile() {
    ${CC} ${CFLAGS} ${CPPFLAGS} ${LDFLAGS} ${S}/thermal_test.c -o ${B}/thermal_test
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/thermal_test ${D}${bindir}/thermal_test
}
