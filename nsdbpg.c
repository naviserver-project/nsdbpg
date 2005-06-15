/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.com/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is AOLserver Code and related documentation
 * distributed by AOL.
 *
 * The Initial Developer of the Original Code is America Online,
 * Inc. Portions created by AOL are Copyright (C) 1999 America Online,
 * Inc. All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License (the "GPL"), in which case the
 * provisions of GPL are applicable instead of those above.  If you wish
 * to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the
 * License, indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by the GPL.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under either the License or the GPL.
 *
 */

/*
 * nsdbpg.c --
 *
 *      Implements the nsdb driver interface for the postgres database.
 */

#include "dbpg.h"

NS_RCSID("$Header$");


#define OID_QUOTED_STRING " oid = '"
#define STRING_BUF_LEN    256


NS_EXPORT int Ns_ModuleVersion = 1;
char *PgDbName = "PostgreSQL";


/*
 * Local functions defined in this file.
 */

static char   *Ns_PgName(Ns_DbHandle *handle);
static int     Ns_PgOpenDb(Ns_DbHandle *dbhandle);
static int     Ns_PgCloseDb(Ns_DbHandle *dbhandle);
static Ns_Set *Ns_PgBindRow(Ns_DbHandle *handle);
static int     Ns_PgExec(Ns_DbHandle *handle, char *sql);
static int     Ns_PgGetRow(Ns_DbHandle *handle, Ns_Set *row);
static int     Ns_PgFlush(Ns_DbHandle *handle);
static int     Ns_PgResetHandle(Ns_DbHandle *handle);

static void Ns_PgUnQuoteOidString(Ns_DString *sql);
static void Ns_PgSetErrorstate(Ns_DbHandle *handle);
static void set_transaction_state(Ns_DbHandle *handle, char *sql, char *funcname);


/*
 * Local variables defined in this file.
 */

static Ns_DbProc PgProcs[] = {
    {DbFn_Name,         Ns_PgName},
    {DbFn_DbType,       Ns_PgName},
    {DbFn_OpenDb,       Ns_PgOpenDb},
    {DbFn_CloseDb,      Ns_PgCloseDb},
    {DbFn_BindRow,      Ns_PgBindRow},
    {DbFn_Exec,         Ns_PgExec},
    {DbFn_GetRow,       Ns_PgGetRow},
    {DbFn_Flush,        Ns_PgFlush},
    {DbFn_Cancel,       Ns_PgFlush},
    {DbFn_ResetHandle,  Ns_PgResetHandle},
    {DbFn_ServerInit,   Ns_PgServerInit},
    {0, NULL}
};

static char          datestyle[STRING_BUF_LEN];
static unsigned int  pgCNum = 0;


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbDriverInit --
 *
 *      Module entry point: register driver functions.
 *
 * Results:
 *      NS_OK or NS_ERROR.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT int
Ns_DbDriverInit(char *hDriver, char *configPath)
{
    char *t;
    char *e;

    /* 
     * Register the PostgreSQL driver functions with nsdb.
     * Nsdb will later call the Ns_PgServerInit() function
     * for each virtual server which utilizes nsdb. 
     */

    if (Ns_DbRegisterDriver(hDriver, &(PgProcs[0])) != NS_OK) {
        Ns_Log(Error, "Ns_DbDriverInit(%s):  Could not register the %s driver.",
               hDriver, PgDbName);
        return NS_ERROR;
    }
    Ns_Log(Notice, "%s loaded.", PgDbName);

    e = getenv("PGDATESTYLE");

    t = Ns_ConfigGetValue(configPath, "DateStyle");

    strcpy(datestyle, "");
    if (t) {
        if (!strcasecmp(t, "ISO") || !strcasecmp(t, "SQL") ||
            !strcasecmp(t, "POSTGRES") || !strcasecmp(t, "GERMAN") ||
            !strcasecmp(t, "NONEURO") || !strcasecmp(t, "EURO")) {
            strcpy(datestyle, "set datestyle to '");
            strcat(datestyle, t);
            strcat(datestyle, "'");
            if (e) {
                Ns_Log(Notice, "PGDATESTYLE overridden by datestyle param.");
            }
        } else {
            Ns_Log(Error, "Illegal value for datestyle - ignored");
        }
    } else {
        if (e) {
            Ns_Log(Notice, "PGDATESTYLE setting used for datestyle.");
        }
    }

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PgName --
 *
 *      Return the string name which identifies the PostgreSQL driver.
 *
 * Results:
 *      Database name.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static char *
Ns_PgName(Ns_DbHandle *ignored) {
    return PgDbName;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PgOpenDb --
 *
 *      Open a connection to a postgres database.  The datasource for
 *      postgres is in the form "host:port:database".
 *
 * Results:
 *      NS_OK or NS_ERROR.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
Ns_PgOpenDb(Ns_DbHandle *handle) {

    static char    *funcname = "Ns_PgOpenDb";
    NsPgConn       *nsConn;
    PGconn         *pgConn;
    char           *host;
    char           *port;
    char           *db;
    char            datasource[STRING_BUF_LEN];

    if (handle == NULL
        || handle->datasource == NULL
        || strlen(handle->datasource) > STRING_BUF_LEN) {

        Ns_Log(Error, "%s: Invalid handle.", funcname);
        return NS_ERROR;
    }

    strcpy(datasource, handle->datasource);
    host = datasource;
    port = strchr(datasource, ':');
    if (port == NULL || ((db = strchr(port + 1, ':')) == NULL)) {
        Ns_Log(Error, "Ns_PgOpenDb(%s):  Malformed datasource:  %s.  Proper form is: (host:port:database).",
               handle->driver, handle->datasource);
        return NS_ERROR;
    } else {
        *port++ = '\0';
        *db++ = '\0';
        if (!strcmp(host, "localhost")) {
            Ns_Log(Notice, "Opening %s on %s", db, host);
            pgConn = PQsetdbLogin(NULL, port, NULL, NULL, db, handle->user,
                                  handle->password);
        } else {
            Ns_Log(Notice, "Opening %s on %s, port %s", db, host, port);
            pgConn = PQsetdbLogin(host, port, NULL, NULL, db, handle->user,
                                  handle->password);
        }

        if (PQstatus(pgConn) == CONNECTION_OK) {
            Ns_Log(Notice, "Ns_PgOpenDb(%s):  Openned connection to %s.",
                   handle->driver, handle->datasource);
            nsConn = ns_malloc(sizeof(NsPgConn));
            if (!nsConn) {
                Ns_Log(Error, "ns_malloc() failed allocating nsConn");
                return NS_ERROR;
            }
            nsConn->in_transaction = NS_FALSE;
            nsConn->cNum = pgCNum++;
            nsConn->conn = pgConn;
            nsConn->res = NULL;
            nsConn->nCols = nsConn->nTuples = nsConn->curTuple = 0;
            handle->connection = nsConn;

            if (strlen(datestyle)) { 
                return Ns_PgExec(handle, datestyle) == NS_DML ?
                    NS_OK : NS_ERROR;
            }
            return NS_OK;
        } else {
            Ns_Log(Error, "Ns_PgOpenDb(%s):  Could not connect to %s:  %s", handle->driver,
                   handle->datasource, PQerrorMessage(pgConn));
            PQfinish(pgConn);
            return NS_ERROR;
        }
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PgCloseDb --
 *
 *      Close an open connection to postgres.
 *
 * Results:
 *      NS_OK or NS_ERROR.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
Ns_PgCloseDb(Ns_DbHandle *handle) {

    static char *funcname = "Ns_PgCloseDb";
    NsPgConn    *nsConn;

    if (handle == NULL || handle->connection == NULL) {
        Ns_Log(Error, "%s: Invalid connection.", funcname);
        return NS_ERROR;
    }

    nsConn = handle->connection;

    if (handle->verbose) {
        Ns_Log(Notice, "Ns_PgCloseDb(%d):  Closing connection:  %s",
               nsConn->cNum, handle->datasource);
    }

    PQfinish(nsConn->conn);

    /* DRB: Not sure why the driver zeroes these out before
     * freeing nsConn, but can't hurt I guess.
    */
    nsConn->conn = NULL;
    nsConn->nCols = nsConn->nTuples = nsConn->curTuple = 0;
    ns_free(nsConn);

    handle->connection = NULL;

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PgBindRow --
 *
 *      Retrieve the column names of the current result.
 *
 * Results:
 *      An Ns_Set whos keys are the names of columns, or NULL on error.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static Ns_Set *
Ns_PgBindRow(Ns_DbHandle *handle)
{
    static char  *funcname = "Ns_PgBindRow";
    int           i;
    NsPgConn      *nsConn;
    Ns_Set        *row = NULL;

    if (handle == NULL || handle->connection == NULL) {
        Ns_Log(Error, "%s: Invalid connection.", funcname);
        goto done;
    }

    if (!handle->fetchingRows) {
        Ns_Log(Error, "%s(%s):  No rows waiting to bind.", funcname, handle->datasource);
        goto done;
    }

    nsConn = handle->connection;    
    row = handle->row;

    if (PQresultStatus(nsConn->res) == PGRES_TUPLES_OK) {
        nsConn->curTuple = 0;
        nsConn->nCols = PQnfields(nsConn->res);
        nsConn->nTuples = PQntuples(nsConn->res);
        row = handle->row;
        
        for (i = 0; i < nsConn->nCols; i++) {
            Ns_SetPut(row, (char *) PQfname(nsConn->res, i), NULL);
        }
       
    }
    handle->fetchingRows = NS_FALSE;
    
  done:
    return (row);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PgExec --
 *
 *      Send SQL to the database.
 *
 * Results:
 *      NS_ROWS, NS_DML or NS_ERROR.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
Ns_PgExec(Ns_DbHandle *handle, char *sql)
{
    static char *funcname = "Ns_PgExec";
    NsPgConn    *nsConn;
    Ns_DString   dsSql;
    int          retry_count = 2;

    if (sql == NULL) {
        Ns_Log(Error, "%s: Invalid SQL query.", funcname);
        return NS_ERROR;
    }

    if (handle == NULL || handle->connection == NULL) {
        Ns_Log(Error, "%s: Invalid connection.", funcname);
        return NS_ERROR;
    }

    nsConn = handle->connection;

    PQclear(nsConn->res);

    Ns_DStringInit(&dsSql);
    Ns_DStringAppend(&dsSql, sql);
    Ns_PgUnQuoteOidString(&dsSql);

    while (dsSql.length > 0 && isspace(dsSql.string[dsSql.length - 1])) {
        dsSql.string[--dsSql.length] = '\0';
    }

    if (dsSql.length > 0 && dsSql.string[dsSql.length - 1] != ';') {
        Ns_DStringAppend(&dsSql, ";");
    }

    if (handle->verbose) {
        Ns_Log(Notice, "Querying '%s'", dsSql.string);
    }

    nsConn->res = PQexec(nsConn->conn, dsSql.string);

    /* Set error result for exception message -- not sure that this
       belongs here in DRB-improved driver..... but, here it is
       anyway, as it can't really hurt anything :-) */
   
    Ns_PgSetErrorstate(handle);

    /* This loop should actually be safe enough, but we'll take no 
     * chances and guard against infinite retries with a counter.
     */

    while (PQstatus(nsConn->conn) == CONNECTION_BAD && retry_count--) {

        int in_transaction = nsConn->in_transaction;

        /* Backend returns a fatal error if it really crashed.  After a crash,
         * all other backends close with a non-fatal error because shared
         * memory might've been corrupted by the crash.  In this case, we
         * will retry the query.
         */

        int retry_query = PQresultStatus(nsConn->res) == PGRES_NONFATAL_ERROR;

        /* Reconnect messages need to be logged regardless of Verbose. */    

        Ns_Log(Notice, "%s: Trying to reopen database connection", funcname);

        PQfinish(nsConn->conn);

        /* We'll kick out with an NS_ERROR if we're in a transaction.
         * The queries within the transaction up to this point were
         * rolled back when the transaction crashed or closed itself
         * at the request of the postmaster.  If we were to allow the
         * rest of the transaction to continue, you'd blow transaction
         * semantics, i.e. the first portion of the transaction would've
         * rolled back and the rest of the transaction would finish its
         * inserts or whatever.  Not good!   So we return an error.  If
         * the programmer's catching transaction errors and rolling back
         * properly, there will be no problem - the rollback will be
         * flagged as occuring outside a transaction but there's no
         * harm in that.
         *
         * If the programmer's started a transaction with no "catch",
         * you'll find no sympathy on my part.
         */

        if (Ns_PgOpenDb(handle) == NS_ERROR || in_transaction || !retry_query) {
            if (in_transaction) {
                Ns_Log(Notice, "%s: In transaction, conn died, error returned",
                       funcname);
            }
            Ns_DStringFree(&dsSql);
            return NS_ERROR;
        }

        nsConn = handle->connection;

        Ns_Log(Notice, "%s: Retrying query", funcname);
        PQclear(nsConn->res);
        nsConn->res = PQexec(nsConn->conn, dsSql.string);

        /* This may again break the connection for two reasons: 
         * our query may be a back-end crashing query or another
         * backend may've crashed after we reopened the backend.
         * Neither's at all likely but we'll loop back and try
         * a couple of times if it does.
         */
    }

    Ns_DStringFree(&dsSql);

    if (nsConn->res == NULL) {
        Ns_Log(Error, "Ns_PgExec(%s):  Could not send query '%s':  %s",
               handle->datasource, sql, PQerrorMessage(nsConn->conn));
        return NS_ERROR;
    }

    /* DRB: let's clean up nsConn a bit, if someone didn't read all
     * the rows returned by a query, did a dml query, then a getrow
     * the driver might get confused if we don't zap nCols and
     * curTuple.
     */
    nsConn->nCols=0;
    nsConn->curTuple=0;
    nsConn->nTuples=0;

    switch(PQresultStatus(nsConn->res)) {
    case PGRES_TUPLES_OK:
        handle->fetchingRows = NS_TRUE;
        return NS_ROWS;
        break;
    case PGRES_COPY_IN:
    case PGRES_COPY_OUT:
        return NS_DML;
        break;
    case PGRES_COMMAND_OK:
        set_transaction_state(handle, sql, funcname);
        sscanf(PQcmdTuples(nsConn->res), "%d", &(nsConn->nTuples));
        return NS_DML;
        break;
    default:
        Ns_Log(Error, "%s: result status: %d message: %s", funcname,
               PQresultStatus(nsConn->res), PQerrorMessage(nsConn->conn));
        Ns_DbSetException(handle,"ERROR",PQerrorMessage(nsConn->conn));
        return NS_ERROR;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PgGetRow --
 *
 *      Fill in the given Ns_Set with values for each column of the
 *      current row.
 *
 * Results:
 *      NS_OK, NS_END_DATA or NS_ERROR.
 *
 * Side effects:
 *      Current tupple updated.
 *
 *----------------------------------------------------------------------
 */

static int
Ns_PgGetRow(Ns_DbHandle *handle, Ns_Set *row) {

    static char *funcname = "Ns_PgGetRow";
    NsPgConn    *nsConn;
    int          i;

    if (handle == NULL || handle->connection == NULL) {
        Ns_Log(Error, "%s: Invalid connection.", funcname);
        return NS_ERROR;
    } 

    if (row == NULL) {
        Ns_Log(Error, "%s: Invalid Ns_Set -> row.", funcname);
        return NS_ERROR;
    }

    nsConn = handle->connection;

    if (nsConn->nCols == 0) {
        Ns_Log(Error, "Ns_PgGetRow(%s):  Get row called outside a fetch row loop.",
               handle->datasource);
        return NS_ERROR;
    } else if (nsConn->curTuple == nsConn->nTuples) {

        PQclear(nsConn->res);
        nsConn->res = NULL;
        nsConn->nCols = nsConn->nTuples = nsConn->curTuple = 0;
        return NS_END_DATA;

    } else {
        for (i = 0; i < nsConn->nCols; i++) {
            Ns_SetPutValue(row, i, PQgetvalue(nsConn->res, nsConn->curTuple, i));
        }
        nsConn->curTuple++;
    }

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PgFlush --
 *
 *      Flush unfetched rows.
 *
 * Results:
 *      NS_OK or NS_ERROR.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
Ns_PgFlush(Ns_DbHandle *handle) {

    static char *funcname = "Ns_PgFlush";
    NsPgConn    *nsConn;

    if (handle == NULL || handle->connection == NULL) {
        Ns_Log(Error, "%s: Invalid connection.", funcname);
        return NS_ERROR;
    }

    nsConn = handle->connection;

    if (nsConn->nCols > 0) {
        PQclear(nsConn->res);
        nsConn->res = NULL;
        nsConn->nCols = nsConn->nTuples = nsConn->curTuple = 0;
    }

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PgResetHandle --
 *
 *      Reset connection ready for next command.
 *
 * Results:
 *      NS_OK or NS_ERROR.
 *
 * Side effects:
 *      Any active transaction will be rolled back.
 *
 *----------------------------------------------------------------------
 */

static int
Ns_PgResetHandle(Ns_DbHandle *handle)
{
    static char *funcname = "Ns_PgResetHandle";
    NsPgConn    *nsConn;

    if (handle == NULL || handle->connection == NULL) {
        Ns_Log(Error, "%s: Invalid connection.", funcname);
        return NS_ERROR;
    }

    nsConn = handle->connection;

    if (nsConn->in_transaction) {
        if (handle->verbose) {
            Ns_Log(Notice, "%s: Rolling back transaction", funcname);
        }
        if (Ns_PgExec(handle, "rollback") != PGRES_COMMAND_OK) {
            Ns_Log(Error, "%s: Rollback failed", funcname);
        }
        return NS_ERROR;
    }

    return NS_OK;
}









/*
 *----------------------------------------------------------------------
 *
 * Ns_PgSetErrorstate --
 *
 *      Propagate the current postgres error message to nsdb.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
Ns_PgSetErrorstate(Ns_DbHandle *handle)
{
    NsPgConn    *nsConn = handle->connection;
    Ns_DString  *nsMsg  = &(handle->dsExceptionMsg);

    Ns_DStringTrunc(nsMsg, 0);

    switch (PQresultStatus(nsConn->res)) {
        case PGRES_EMPTY_QUERY:
        case PGRES_COMMAND_OK:
        case PGRES_TUPLES_OK:
        case PGRES_COPY_OUT:
        case PGRES_COPY_IN:
        case PGRES_NONFATAL_ERROR:
              Ns_DStringAppend(nsMsg, PQresultErrorMessage(nsConn->res));
              break;

        case PGRES_FATAL_ERROR:
              Ns_DStringAppend(nsMsg, PQresultErrorMessage(nsConn->res));
              break;

        case PGRES_BAD_RESPONSE:
              Ns_DStringAppend(nsMsg, "PGRES_BAD_RESPONSE ");
              Ns_DStringAppend(nsMsg, PQresultErrorMessage(nsConn->res));
              break;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * set_transaction_state --
 *
 *      Set the current transaction state based on the query pointed
 *      to by "sql".  Should be called only after the query has
 *      successfully been executed.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
set_transaction_state(Ns_DbHandle *handle, char *sql, char *funcname)
{
    NsPgConn *nsConn = handle->connection;

    while (*sql == ' ') sql++;

    if (!strncasecmp(sql, "begin", 5)) {
        if (handle->verbose) {
            Ns_Log(Notice, "%s: Entering transaction", funcname);
        }
        nsConn->in_transaction = NS_TRUE;
    } else if (!strncasecmp(sql, "end", 3) ||
               !strncasecmp(sql, "commit", 6)) {
        if (handle->verbose) {
            Ns_Log(Notice, "%s: Committing transaction", funcname);
        }
        nsConn->in_transaction = NS_FALSE;
    } else if (!strncasecmp(sql, "abort", 5) ||
               !strncasecmp(sql, "rollback", 8)) {
        if (handle->verbose) {
            Ns_Log(Notice, "%s: Rolling back transaction", funcname);
        }
        nsConn->in_transaction = NS_FALSE;
    }
}

static void
Ns_PgUnQuoteOidString(Ns_DString *sql) {

    static char *funcname = "Ns_PgUnQuoteOidString";
    char *ptr;

    if (sql == NULL) {
        Ns_Log(Error, "%s: Invalid Ns_DString -> sql.", funcname);
        return;
    }

/* Additions by Scott Cannon Jr. (SDL/USU):
 *
 * This corrects the problem of quoting oid numbers when requesting a
 * specific row.
 *
 * The quoting of oid's is currently ambiguous.
 */

    if ((ptr = strstr(sql->string, OID_QUOTED_STRING)) != NULL) {
        ptr += (strlen(OID_QUOTED_STRING) - 1);
        *ptr++ = ' ';
        while(*ptr != '\0' && *ptr != '\'') {
            ptr++;
        }
        if (*ptr == '\'') {
            *ptr = ' ';
        }
    }
}
