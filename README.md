## What is this?

This module provides a straightforward database services driver for NaviServer. It acts as an intermediary between the NaviServer database-independent `nsdb` module and the API of a specific DBMS. In essence, the driver establishes connections, executes SQL commands, and converts the results into the format required by `nsdb`. This driver is designed for the PostgreSQL ORDBMS, developed by The PostgreSQL Global Development Group, and serves as the official driver for the OpenACS project. Since PostgreSQL is available on most Unix systems, ensure that it is installed on your system before using this driver. For additional details or to download PostgreSQL, visit:

    http://www.postgresql.org

**Compatibility:** This module compiles with Tcl versions 8.5, 8.6, and 9.0.

## What's New?

For the latest updates and modifications, please refer to the Changelog.

## How Does It Work?

Database driver modules for NaviServer resemble typical modules but are loaded in a different manner. Instead of being declared under the `[ns/server/<server-name>/modules]` section, a database driver is registered in the `[ns/db/drivers]` section, and loading is managed automatically by `nsdb`. Typically, the driver’s initialization function simply calls `nsdb`'s `Ns_DbRegisterDriver()` with an array of function pointers. These functions later facilitate database connections, query execution, and result processing—a design approach similar to ODBC on Windows.

In addition to basic functions for opening connections, selecting data, and retrieving rows, the driver also supplies system catalog functions and a virtual server initialization routine. Each time `nsdb` is loaded into a virtual server, the initialization function `Ns_PgServerInit` is executed. This routine registers the `"ns_pg"` Tcl command in the server’s Tcl interpreters, enabling Tcl scripts to access information about an active PostgreSQL connection.

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

---

## Sample Configuration

Below is an example configuration snippet for NaviServer. This example assumes that the Tcl variables `db_host`, `db_port`, `db_name`, and `db_user` have been defined appropriately.

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

If your database connection requires SSL, append the appropriate PostgreSQL "conninfo" parameters to the datasource:

```tcl
...
   ns_param   datasource         "${db_host}:${db_port}:dbname=${db_name} sslmode=require"
...
```

---

## Provided API

### ns_pg_bind

`ns_pg_bind` is an enhanced version of the `ns_db` API for executing PostgreSQL statements with support for bind variables (denoted by a colon `:` in SQL statements). The bind variable values can be supplied via an `ns_set` using the `-bind` option or derived from the current calling environment.

```tcl
ns_pg_bind dml     /handle/ -bind /bind/ /sql/
ns_pg_bind 1row    /handle/ -bind /bind/ /sql/
ns_pg_bind 0or1row /handle/ -bind /bind/ /sql/
ns_pg_bind select  /handle/ -bind /bind/ /sql/
ns_pg_bind exec    /handle/ -bind /bind/ /sql/
```

### ns_pg

The `ns_pg` command provides direct interaction with the PostgreSQL client library, including support for BLOB operations (note that BLOB support may be outdated). The available `ns_pg` subcommands include:

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

The `ns_pg_prepare` command returns a dictionary representing a prepared statement for the given SQL command. This dictionary includes the keys `"sql"` and `"args"`.

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
PGPATH=postgresql17 && make PGLIB=/opt/local/lib/$PGPATH/ PGINCLUDE=/opt/local/include/$PGPATH/
```

For additional code checking using cppcheck, consider:

```bash
PGPATH=postgresql17 && make PGLIB=/opt/local/lib/$PGPATH/ PGINCLUDE=/opt/local/include/$PGPATH/ cppcheck
```

Refer to the Makefile for further details regarding the module build process.
