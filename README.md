## What is this?

This module provides a straightforward database services driver for
NaviServer. It acts as an intermediary between the NaviServer
database-independent `nsdb` module and the API of a specific DBMS. In
essence, the driver establishes connections, executes SQL commands,
and converts the results into the format required by `nsdb`. This
driver is designed for the PostgreSQL ORDBMS, developed by The
PostgreSQL Global Development Group, and serves as the official driver
for the OpenACS project. Since PostgreSQL is available on most Unix
systems, ensure that it is installed on your system before using this
driver. For additional details or to download PostgreSQL, visit:

    http://www.postgresql.org

**Compatibility:** This module compiles with Tcl versions 8.5, 8.6, and 9.0.

## What's New?

For the latest updates and modifications, please refer to the Changelog.

## How Does It Work?

Database driver modules for NaviServer resemble typical modules but
are loaded in a different manner. Instead of being declared under the
`[ns/server/<server-name>/modules]` section, a database driver is
registered in the `[ns/db/drivers]` section, and loading is managed
automatically by `nsdb`. Typically, the driver’s initialization
function simply calls `nsdb`'s `Ns_DbRegisterDriver()` with an array
of function pointers. These functions later facilitate database
connections, query execution, and result processing—a design approach
similar to ODBC on Windows.

In addition to basic functions for opening connections, selecting
data, and retrieving rows, the driver also supplies system catalog
functions and a virtual server initialization routine. Each time
`nsdb` is loaded into a virtual server, the initialization function
`Ns_PgServerInit` is executed. This routine registers the `"ns_pg"`
Tcl command in the server’s Tcl interpreters, enabling Tcl scripts to
access information about an active PostgreSQL connection.

**Contributors to this file include:**

- Don Baccus `<dhogaza@pacifier.com>`
- Lamar Owen `<lamar.owen@wgcr.org>`
- Jan Wieck `<wieck@debis.com>`
- Keith Pasket (SDL/USU)
- Scott Cannon, Jr. (SDL/USU)
- Dan Wickstrom `<danw@rtp.ericsson.se>`
- Scott S. Goodwin `<scott@scottg.net>`
- Gustaf Neumann `<neumann@wu-wien.ac.at>`

*Original example driver developed by Jim Davidson.*


## Runtime version information

When built with a recent NaviServer version, nsdbpg reports its module
version and source revision through `ns_server modules`.

---

## Sample Configuration

Below is an example configuration snippet for NaviServer. This example
assumes that the Tcl variables `db_host`, `db_port`, `db_name`, and
`db_user` have been defined appropriately.

```tcl
ns_section ns/db/pools {
   ns_param   pool1              "Pool 1"
}
ns_section ns/db/pool/pool1 {
   # ns_param  maxidle            0
   # ns_param  maxopen            0
   ns_param   connections        15
   ns_param   LogMinDuration     0.01   ;# Only log SQL statements that exceed this duration when SQL logging is enabled
   ns_param   logsqlerrors       true
   ns_param   driver             postgres
   ns_param   datasource         "${db_host}:${db_port}:dbname=${db_name}"
   ns_param   user               $db_user
   ns_param   password           ""
}
```

If your database connection requires SSL, append the appropriate
PostgreSQL "conninfo" parameters to the datasource:

```tcl
...
   ns_param   datasource         "${db_host}:${db_port}:dbname=${db_name} sslmode=require"
...
```

---

## Provided API

### ns_pg_bind

`ns_pg_bind` is an enhanced version of the `ns_db` API for executing
PostgreSQL statements with support for bind variables (denoted by a
colon `:` in SQL statements). The bind variable values can be supplied
via an `ns_set` using the `-bind` option or derived from the current
calling environment.

```tcl
ns_pg_bind dml     /handle/ -bind /bind/ /sql/
ns_pg_bind 1row    /handle/ -bind /bind/ /sql/
ns_pg_bind 0or1row /handle/ -bind /bind/ /sql/
ns_pg_bind select  /handle/ -bind /bind/ /sql/
ns_pg_bind exec    /handle/ -bind /bind/ /sql/
```

### ns_pg

The `ns_pg` command provides direct interaction with the PostgreSQL
client library, including support for BLOB operations (note that BLOB
support may be outdated). The available `ns_pg` subcommands include:

```tcl
ns_pg blob_dml_file    /handle/ /blobId/ /filename/
ns_pg blob_get         /handle/ /blobId/
ns_pg blob_put         /handle/ /blobId/ /value/
ns_pg blob_select_file /handle/ /blobId/ /filename/
ns_pg blob_write       /handle/ /blobId/
ns_pg db               /handle/
ns_pg error            /handle/
ns_pg host             /handle/
ns_pg ntuples          /handle/
ns_pg number           /handle/
ns_pg options          /handle/
ns_pg pid              /handle/
ns_pg port             /handle/
ns_pg status           /handle/
```

### ns_pg_prepare /sql/

The `ns_pg_prepare` command returns a dictionary representing a
prepared statement for the given SQL command. This dictionary includes
the keys `"sql"` and `"args"`.

---

## Developer's Guide

For detailed developer information, run:

```bash
make help
```

### Build Requirements

The module requires the following from PostgreSQL:

- The header file `libpq-fe.h`
- The PostgreSQL library files (`libpq.*`)

**Example Build Operations:**

```bash
export PG=/usr/local/pg17
make PGLIB=$PG/lib PGINCLUDE=$PG/include

make PGINCLUDE=/usr/include/postgresql
```

To build and install the module when PostgreSQL is located in a non-standard directory, use:

```bash
export PG=/usr/local/pg17
make PGLIB=$PG/lib PGINCLUDE=$PG/include
make PGLIB=$PG/lib PGINCLUDE=$PG/include install
```

If NaviServer is installed in a non-default location, adjust your commands accordingly:

```bash
make PGLIB=... PGINCLUDE=... NAVISERVER=/opt/local/ns499/
make PGLIB=... PGINCLUDE=... NAVISERVER=/opt/local/ns499/ install
```

On systems using MacPorts, you might execute:

```bash
PGPATH=postgresql17 make PGLIB=/opt/local/lib/$PGPATH/ PGINCLUDE=/opt/local/include/$PGPATH/
```

For additional code checking using cppcheck, consider:

```bash
PGPATH=postgresql17 make PGLIB=/opt/local/lib/$PGPATH/ PGINCLUDE=/opt/local/include/$PGPATH/ cppcheck
```

Refer to the Makefile for further details regarding the module build process.

## PostgreSQL notices

Database-side `RAISE NOTICE`, `RAISE WARNING` and other libpq notices go through
NaviServer logging, with the pool name and the executing thread's timestamp and
context. `DETAIL`, `HINT` and `CONTEXT` are continuation fields of the same log
record. DEBUG maps to Debug; LOG, INFO and NOTICE to Notice; WARNING to Warning;
ERROR to Error; FATAL and PANIC to Fatal. Normal NaviServer severity filtering
applies. PostgreSQL `client_min_messages` still controls which messages reach
the client. When available, nonlocalized PostgreSQL severity fields are used.
The callback does not change SQL success or exception handling. It is installed
on each connection before initialization SQL, including replacement connections.

### Notice regression tests

Build the driver, then run against a dedicated PostgreSQL test database (PL/pgSQL
must be available). The tests create no persistent database objects:

```sh
NSDBPG_TEST_DATASOURCE=127.0.0.1:5432:testdb NSDBPG_TEST_USER=testuser \
  /usr/local/ns/bin/nsd -c -d -t "$PWD/tests/notice.nscfg" "$PWD/tests/run-notice.tcl"
```

Optional environment variables: `NSDBPG_TEST_HOME` (NaviServer installation),
`NSDBPG_TEST_PASSWORD` (database password). The test configuration loads the
built `nsdbpg.so` from this repository. It requires no OpenACS installation.

## Core ns_db bind options

On NaviServer versions providing the core bind-variable callbacks, the driver
supports the following options on `ns_db exec`, `dml`, `select`, `1row` and
`0or1row`:

* `-bindvars`: values from variables in the caller's Tcl scope.
* `-bindset $set`: values from an `ns_set`.
* `-binddict $dict`: values from a Tcl dictionary.
* `-emptynull`: explicitly map empty scalar/binary values and collection cells
  to SQL NULL. Without it, empty text and empty bytea remain empty values.
* `-bindtypes $types`: scalar, list, rows, binary, native array, and per-column row types.

Exactly one binding source is required when using these options. Missing values
are errors; explicit sources do not fall back to caller variables. The core
validates and expands collections, caches the SQL conversion, and translates
colon names to libpq's positional parameter syntax. Callers continue writing
`:name`; positional markers are an internal implementation detail.

```tcl
set id 42
set row [ns_db 1row $h -bindvars {
    SELECT name FROM users WHERE user_id=:id
}]

set row [ns_db 1row $h -binddict {id 42} {
    SELECT name FROM users WHERE user_id=:id
}]

set ids {1 2 3}
set row [ns_db select $h -bindvars -bindtypes {ids list} {
    SELECT user_id FROM users WHERE user_id IN (:ids)
}]

set values [list \
    [list 1 [binary format H* 00ff] first] \
    [list 2 [binary format H* {}] second]]
ns_db dml $h -bindvars \
    -bindtypes {values {rows {scalar binary scalar}}} {
        INSERT INTO example(id,payload,label) VALUES :values
    }
```

The new path uses `PQexecParams`, keeping parameter data separate from SQL.
PostgreSQL infers scalar SQL types from context; use explicit SQL casts where
needed. Explicit binary values are PostgreSQL `bytea` parameters, not the wire
representations of arbitrary PostgreSQL types. Empty binary values and embedded
NUL bytes are preserved. Text values use the same Tcl external encoding convention
as existing driver calls; embedded NUL in text is rejected. Results keep the
existing text-format `ns_set` representation, including PostgreSQL's normal
textual representation of bytea.

The extended query protocol accepts a single SQL statement per call and at most
65,535 parameters after expansion. List and row bindings must be nonempty; row
widths must match. SQL identifiers and SQL fragments are not parameter values.
The core's documented binding lexer supports ordinary quoted strings,
identifiers, comments and PostgreSQL casts. PostgreSQL escape strings,
dollar-quoted bodies and nested block comments are not supported by that lexer;
use ordinary execution without the new options for such SQL. The driver does
not add another parser or silently change the shared lexer.

This implementation requires the core fix applying a driver's marker factory
to scalar parameters as well as expanded collections. Build and deploy the
matching core and driver together to enable the new options. The driver now
registers one typed callback for both text and binary parameters; a NULL types
array means all text. The core must provide generalized typed-only dispatch.
Existing text-only callbacks in other drivers remain supported by that core. The new path does
not retry a potentially submitted statement after connection loss; callers
retain responsibility for transaction recovery.

### Backward compatibility

`ns_pg_bind`, its `-bind $nsset` option, ordinary unbound `ns_db` execution,
row fetching, notices, pseudo-BLOB functions and their existing implementations
are unchanged. In particular, legacy `ns_pg_bind` retains its empty-to-NULL
mapping; the new core options require `-emptynull` to request that mapping.
No OpenACS application needs to migrate to the new interface.

The optional callbacks are compiled only when the NaviServer headers provide
both bind execution and parameter-marker interfaces. Building against earlier
NaviServer headers omits them and retains the legacy commands. This is source
compatibility: a driver binary must be built for its target NaviServer/Tcl ABI.

### Integration tests

Use a dedicated PostgreSQL test database:

```sh
NSDBPG_TEST_DATASOURCE=localhost:5432:driver_test \
NSDBPG_TEST_USER=test_user \
make test-bind
```

`PGHOST` can select a Unix socket directory when the datasource host is
`localhost`. Optional overrides are `NSDBPG_TEST_HOME`, `NSDBPG_TEST_PASSWORD`,
`NSDBPG_TEST_TCLLIB` and `NSDBPG_TEST_NSDB_MODULE`. Tests create temporary tables
and require no OpenACS installation. The test account must be able to create
those temporary tables.

## One-dimensional SQL arrays

Core Tcl binding syntax version 6 and `NS_DB_BIND_ABI >= 2` add one native array
parameter, distinct from expanding a list into many SQL parameters:

```tcl
set ids {1 3 5}
set row [ns_db select $h -bindvars -bindtypes {ids array} {
    SELECT user_id FROM users WHERE user_id = ANY(:ids::integer[])
}]
```

The destination column or an explicit SQL cast determines the element type.
`array` accepts a Tcl list of scalar text elements. It always encodes one
dimension; nested Tcl list elements are scalar strings, never inferred SQL
subarrays. Binary array-element interpretation and multidimensional arrays are
not supported by this metadata. Existing scalar, binary, list and rows meanings
are unchanged.

An empty Tcl list binds an empty array, including with `-emptynull`. Without that
option, empty elements remain empty strings. With `-emptynull`, only zero-length
elements become SQL NULL. Literal strings such as `NULL`, `null`, `{}` and spaces
remain text values. SQL NULL elements use actual NULL pointers in the shared
prepared representation, not a magic string marker.

PostgreSQL's driver quotes each text element and escapes quotes/backslashes,
then sends the encoded array as one text parameter through libpq. A literal
`NULL` element is quoted; only a prepared NULL pointer becomes the array input
syntax's unquoted NULL token. Embedded NUL text is rejected. The input Tcl list
and elements are not rewritten. Results retain the driver's existing textual
array representation; this change does not introduce array decoding.

Array preparation retains the Tcl list without eagerly generating its string
representation. Text elements use the same external encoding as scalar bind
values before PostgreSQL array escaping. Known integer objects (including wide
integers and bignums) retain their numeric text directly, as in `ns_pg_bind`,
without external conversion or escape scanning. String objects are not parsed
or coerced into integers. Consecutive text elements referencing the
same Tcl object reuse their encoded form within that call; no encoded array
state is retained across executions. Empty-to-NULL handling is unchanged.

The default delimiter is a comma. For element types with another PostgreSQL
input delimiter, provide it explicitly, for example box[]:

```tcl
set boxes [list {(1,1),(0,0)} {(3,3),(2,2)}]
set row [ns_db 1row $h -bindvars -bindtypes {boxes {array ;}} {
    SELECT cardinality(:boxes::box[]) AS n
}]
```

A delimiter must be one printable non-reserved ASCII character; braces, double
quotes, backslashes and whitespace are rejected. The driver does not parse SQL
casts or query the catalog to guess type-specific delimiters.

Check native parameter support using:

```tcl
ns_db bindinfo $h -types
# PostgreSQL: scalar binary array
```

Unsupported drivers, including SQLite and older typed drivers without an array
capability declaration, fail explicitly before SQL execution. `list` and `rows`
remain supported core expansions and are not included in the native-type list.

Build and deploy the matching core library/headers and PostgreSQL driver for
array support. Existing callback signatures and IDs are preserved; the new type
and optional type-capability callback are additive. Drivers built with earlier
headers retain their previous text/binary or legacy interface without array
support. Legacy `ns_pg_bind` remains unchanged and does not interpret this new
metadata.
