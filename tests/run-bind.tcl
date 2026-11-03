package require tcltest 2.2
namespace import ::tcltest::test
::tcltest::configure -verbose {body error}
set ::bindHandle [ns_db gethandle bind-test]
try {
    ns_db dml $::bindHandle {CREATE TEMP TABLE bind_items(id integer PRIMARY KEY,v text,b bytea)}
    source [file join [file dirname [info script]] bind.test]
} finally {
    ns_db releasehandle $::bindHandle
}
set failed $::tcltest::numTests(Failed)
::tcltest::cleanupTests
ns_shutdown
exit [expr {$failed>0}]
