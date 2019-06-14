# 4-Streaming-replication.md
### elog(LOG, "_data_len %d", len);
~~~
/*
		 * Start streaming.
		 *
		 * We'll try to start at the requested starting point and timeline,
		 * even if it's different from the server's latest timeline. In case
		 * we've already reached the end of the old timeline, the server will
		 * finish the streaming immediately, and we will go back to await
		 * orders from the startup process. If recovery_target_timeline is
		 * 'latest', the startup process will scan pg_xlog and find the new
		 * history file, bump recovery target timeline, and ask us to restart
		 * on the new timeline.
		 */
		ThisTimeLineID = startpointTLI;
		if (walrcv_startstreaming(startpointTLI, startpoint,
								  slotname[0] != '\0' ? slotname : NULL))
		{
			if (first_stream)
				ereport(LOG,
						(errmsg("started streaming WAL from primary at %X/%X on timeline %u",
							(uint32) (startpoint >> 32), (uint32) startpoint,
								startpointTLI)));
			else
				ereport(LOG,
				   (errmsg("restarted WAL streaming at %X/%X on timeline %u",
						   (uint32) (startpoint >> 32), (uint32) startpoint,
						   startpointTLI)));
			first_stream = false;

			/* Initialize LogstreamResult and buffers for processing messages */
			LogstreamResult.Write = LogstreamResult.Flush = GetXLogReplayRecPtr(NULL);
			initStringInfo(&reply_message);
			initStringInfo(&incoming_message);

			/* Initialize the last recv timestamp */
			last_recv_timestamp = GetCurrentTimestamp();
			ping_sent = false;

			/* Loop until end-of-streaming or error */
			for (;;)
			{
				char	   *buf;
				int			len;
				bool		endofwal = false;
				pgsocket	wait_fd = PGINVALID_SOCKET;
				int			rc;

				/*
				 * Exit walreceiver if we're not in recovery. This should not
				 * happen, but cross-check the status here.
				 */
				if (!RecoveryInProgress())
					ereport(FATAL,
							(errmsg("cannot continue WAL streaming, recovery has already ended")));

				/* Process any requests or signals received recently */
				ProcessWalRcvInterrupts();

				if (got_SIGHUP)
				{
					got_SIGHUP = false;
					ProcessConfigFile(PGC_SIGHUP);
					XLogWalRcvSendHSFeedback(true);
				}

				/* See if we can read data immediately */
				len = walrcv_receive(&buf, &wait_fd);
				elog(LOG, "_data_len %d", len);
				if (len != 0)
				{
					/*
					 * Process the received data, and any subsequent data we
					 * can read without blocking.
					 */
					for (;;)
					{
						if (len > 0)
						{
							/*
							 * Something was received from master, so reset
							 * timeout
							 */
							last_recv_timestamp = GetCurrentTimestamp();
							ping_sent = false;
							XLogWalRcvProcessMsg(buf[0], &buf[1], len - 1);
						}
						else if (len == 0)
							break;
						else if (len < 0)
						{
							ereport(LOG,
									(errmsg("replication terminated by primary server"),
									 errdetail("End of WAL reached on timeline %u at %X/%X.",
											   startpointTLI,
											   (uint32) (LogstreamResult.Write >> 32), (uint32) LogstreamResult.Write)));
							endofwal = true;
							break;
						}
						len = walrcv_receive(&buf, &wait_fd);
					}

					
~~~

~~~
rm -rf ~/project/DemoDb/
make clean
make
make install
initdb
 ./postgres -D ~/project/DemoDb
euloo@euloo:~/project/pgsql$ psql postgres 
psql (9.6.12)
Type "help" for help.

postgres=# CREATE ROLE repl WITH REPLICATION PASSWORD 'password' LOGIN;
CREATE ROLE
postgres=# CREATE ROLE
postgres-# 
euloo@euloo:~/project/DemoDb$ cat pg_hba.conf
# PostgreSQL Client Authentication Configuration File
# ===================================================
#
# Refer to the "Client Authentication" section in the PostgreSQL
# documentation for a complete description of this file.  A short
# synopsis follows.
#
# This file controls: which hosts are allowed to connect, how clients
# are authenticated, which PostgreSQL user names they can use, which
# databases they can access.  Records take one of these forms:
#
# local      DATABASE  USER  METHOD  [OPTIONS]
# host       DATABASE  USER  ADDRESS  METHOD  [OPTIONS]
# hostssl    DATABASE  USER  ADDRESS  METHOD  [OPTIONS]
# hostnossl  DATABASE  USER  ADDRESS  METHOD  [OPTIONS]
#
# (The uppercase items must be replaced by actual values.)
#
# The first field is the connection type: "local" is a Unix-domain
# socket, "host" is either a plain or SSL-encrypted TCP/IP socket,
# "hostssl" is an SSL-encrypted TCP/IP socket, and "hostnossl" is a
# plain TCP/IP socket.
#
# DATABASE can be "all", "sameuser", "samerole", "replication", a
# database name, or a comma-separated list thereof. The "all"
# keyword does not match "replication". Access to replication
# must be enabled in a separate record (see example below).
#
# USER can be "all", a user name, a group name prefixed with "+", or a
# comma-separated list thereof.  In both the DATABASE and USER fields
# you can also write a file name prefixed with "@" to include names
# from a separate file.
#
# ADDRESS specifies the set of hosts the record matches.  It can be a
# host name, or it is made up of an IP address and a CIDR mask that is
# an integer (between 0 and 32 (IPv4) or 128 (IPv6) inclusive) that
# specifies the number of significant bits in the mask.  A host name
# that starts with a dot (.) matches a suffix of the actual host name.
# Alternatively, you can write an IP address and netmask in separate
# columns to specify the set of hosts.  Instead of a CIDR-address, you
# can write "samehost" to match any of the server's own IP addresses,
# or "samenet" to match any address in any subnet that the server is
# directly connected to.
#
# METHOD can be "trust", "reject", "md5", "password", "gss", "sspi",
# "ident", "peer", "pam", "ldap", "radius" or "cert".  Note that
# "password" sends passwords in clear text; "md5" is preferred since
# it sends encrypted passwords.
#
# OPTIONS are a set of options for the authentication in the format
# NAME=VALUE.  The available options depend on the different
# authentication methods -- refer to the "Client Authentication"
# section in the documentation for a list of which options are
# available for which authentication methods.
#
# Database and user names containing spaces, commas, quotes and other
# special characters must be quoted.  Quoting one of the keywords
# "all", "sameuser", "samerole" or "replication" makes the name lose
# its special character, and just match a database or username with
# that name.
#
# This file is read on server startup and when the postmaster receives
# a SIGHUP signal.  If you edit the file on a running system, you have
# to SIGHUP the postmaster for the changes to take effect.  You can
# use "pg_ctl reload" to do that.

# Put your actual configuration here
# ----------------------------------
#
# If you want to allow non-local connections, you need to add more
# "host" records.  In that case you will also need to make PostgreSQL
# listen on a non-local interface via the listen_addresses
# configuration parameter, or via the -i or -h command line switches.

# CAUTION: Configuring the system for local "trust" authentication
# allows any local user to connect as any PostgreSQL user, including
# the database superuser.  If you do not trust all your local users,
# use another authentication method.


# TYPE  DATABASE        USER            ADDRESS                 METHOD

# "local" is for Unix domain socket connections only
local   all             all                                     trust
# IPv4 local connections:
host    all             all             127.0.0.1/32            trust
host  replication  repl              127.0.0.1/32  md5
# IPv6 local connections:
host    all             all             ::1/128                 trust
# Allow replication connections from localhost, by a user with the
# replication privilege.
#local   replication     euloo                                trust
#host    replication     euloo        127.0.0.1/32            trust
#host    replication     euloo        ::1/128                 trust

~~~