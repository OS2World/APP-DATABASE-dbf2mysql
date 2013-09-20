/* This program reads in an xbase-dbf file and sends 'inserts' to an
   mySQL-server with the records in the xbase-file

   M. Boekhold (boekhold@cindy.et.tudelft.nl)  okt. 1995
   Patched for mySQL by Michael Widenius (monty@analytikerna.se) 96.11.03
*/

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <mysql.h>
#include "dbf.h"

int	verbose = 0, upper = 0, lower = 0, create = 0, fieldlow = 0, var_chars=1,
	null_fields=0, trim = 0, quick = 0;
char	primary[11];
char	*host = NULL;
char 	*dbase = "test";
char 	*table = "test";
char	*subarg = NULL;
char	*flist = NULL;
char	*indexes = NULL;
char	*convert = NULL;

#ifdef __EMX__
/* needed by OS/2 libmysqlclient */
#define MY_INIT(name) { my_progname=name; my_init(); }
extern char * my_progname;
#endif

void do_onlyfields (char *flist, dbhead *dbh);
void do_substitute(char *subarg, dbhead *dbh);
inline void strtoupper(char *string);
inline void strtolower(char *string);
void do_create(MYSQL *, char*, dbhead*);
void do_inserts(MYSQL *, char*, dbhead*);
int check_table(MYSQL *, char*);
void usage(void);

inline void strtoupper(char *string) {
	while(*string != '\0') {
		*string = toupper(*string);
		string++;
	}
}

inline void strtolower(char *string) {
	while(*string != '\0') {
		*string = tolower(*string);
		string++;
	}
}


int check_table(MYSQL *sock, char *table) {
	MYSQL_RES	*result;
	MYSQL_ROW	row;

        if ( (result = mysql_list_tables (sock,NULL)) == NULL )
          return 0;
	while ( (row = mysql_fetch_row(result)) != NULL)
	{
		if ( strcmp((char *) row[0], table) == 0) {
			mysql_free_result(result);
			return 1;
		}
	}
	mysql_free_result(result);
	return 0;
}

void usage(void){
		printf("dbf2mysql %s\n", VERSION);
		printf("usage: dbf2mysql [-h hostname] [-d dbase] [-t table] [-p primary key]\n");
		printf("                 [-o field[,field]] [-s oldname=newname[,oldname=newname]]\n");
		printf("                 [-i field[,field]] [-c] [-f] [-F] [-n] [-r] [-u|-l] \n"); 
		printf("                 [-v[v]] [-q] dbf-file\n");
}

/* patch by Mindaugas Riauba <minde@pub.osf.lt> */
/* Nulls non specified field names */

void do_onlyfields (char *flist, dbhead *dbh)
{
  char *s, *flist2;
  int i, match;

  if (flist == NULL)  return;

  if (verbose > 1)
    printf ("Removing not specified fields\n");
  
  if ( (flist2 = malloc (strlen(flist)*sizeof(char) + 1)) == NULL)
  {
	fprintf (stderr, "Memory allocation error in function do_onlyfields\n");
	close(dbh->db_fd);
	free(dbh);
	exit(1);
  }
  
  if (verbose > 1)
    printf ("Used fields: ");
  for (i = 0; i < dbh -> db_nfields; i++)
  { 
    strcpy (flist2, flist);
    match = 0;
    for (s = strtok (flist2, ","); s != NULL; s = strtok (NULL, ","))
    {
      if ( stricmp (s, dbh -> db_fields[i].db_name) == 0 )
      {
        match = 1;
        if (verbose>1)
          printf ("%s", s);
      }  
    }
    if ( match == 0 )  dbh -> db_fields[i].db_name[0] = '\0';
  }
  if (verbose > 1)
    printf ("\n");
  free (flist2);
}  /* do_onlyfields */

/* patch submitted by Jeffrey Y. Sue <jysue@aloha.net> */
/* Provides functionallity for substituting dBase-fieldnames for others */
/* Mainly for avoiding conflicts between fieldnames and mySQL-reserved */
/* keywords */

void do_substitute(char *subarg, dbhead *dbh)
{
  /* NOTE: subarg is modified in this function */
  int i,bad;
  char *p,*oldname,*newname;
  if (subarg == NULL)  return;

  if (verbose>1) {
    printf("Substituting new field names\n");
  }
  /* use strstr instead of strtok because of possible empty tokens */
  oldname = subarg;
  while (oldname && strlen(oldname) && (p=strstr(oldname,"=")) ) {
    *p = '\0';      /* mark end of oldname */
    newname = ++p;  /* point past \0 of oldname */
    if (strlen(newname)) {  /* if not an empty string */
      p = strstr(newname,",");
      if (p) {
	*p = '\0';      /* mark end of newname */
	p++;    /* point past where the comma was */
      }
    }
    if (strlen(newname)>=DBF_NAMELEN) {
      printf("Truncating new field name %s to %d chars\n",
	     newname,DBF_NAMELEN-1);
      newname[DBF_NAMELEN-1] = '\0';
    }
    bad = 1;
    for (i=0;i<dbh->db_nfields;i++) {
      if (strcmp(dbh->db_fields[i].db_name,oldname)==0) {
	bad = 0;
	strcpy(dbh->db_fields[i].db_name,newname);
	if (verbose>1) {
	  printf("Substitute old:%s new:%s\n",
		 oldname,newname);
	}
	break;
      }
    }
    if (bad) {
      printf("Warning: old field name %s not found\n",
	     oldname);
    }
    oldname = p;
  }
} /* do_substitute */

void do_create(MYSQL *SQLsock, char *table, dbhead *dbh) {
	char		*query, *s;
	char 		t[20];
	int		i, length;

	if (verbose > 1) {
		printf("Building CREATE-clause\n");
	}

	if (!(query = (char *)malloc(
			(dbh->db_nfields * 60) + 29 + strlen(table)))) {
		fprintf(stderr, "Memory allocation error in function do_create\n");
		mysql_close(SQLsock);
		close(dbh->db_fd);
		free(dbh);
		exit(1);
	}

	sprintf(query, "CREATE TABLE %s (", table);
	length = strlen(query);
	for ( i = 0; i < dbh->db_nfields; i++) {
              if (!strlen(dbh->db_fields[i].db_name)) {
                      continue;
                                 /* skip field if length of name == 0 */
              }
		if ((strlen(query) != length)) {
                        strcat(query, ",");
                }

		if (fieldlow)
			strtolower(dbh->db_fields[i].db_name);

                strcat(query, dbh->db_fields[i].db_name);
                switch(dbh->db_fields[i].db_type) {
						case 'D':
                        case 'C':
			        if (var_chars)
				  strcat(query, " varchar");
				else
				  strcat(query, " char");
                                sprintf(t," (%d)",dbh->db_fields[i].db_flen);
                                strcat(query, t);
                                break;
                        case 'N':
                                if (dbh->db_fields[i].db_dec != 0) {
                                        strcat(query, " real");
                                } else {
                                        strcat(query, " int");
                                }
                                break;
                        case 'L':
                                strcat(query, " char (1)");
                                break;
                }
                if (strcmp(dbh->db_fields[i].db_name, primary) == 0) {
					strcat(query, " not null primary key");
				}
		else if (!null_fields)
		  strcat(query," NOT NULL");
	}

	if (indexes)  /* add INDEX statements */
	{
	  for (s = strtok (indexes, ","); s != NULL; s = strtok (NULL, ","))
	  {
	    strcat (query, ",INDEX(");
	    strcat (query, s);
	    strcat (query, ")");
	  }
	}

	strcat(query, ")\n");

	if (verbose > 1) {
		printf("Sending create-clause\n");
		printf("%s\n", query);
		printf ("fields in dbh %d, allocated mem for query %d, query size %d\n",
		        dbh->db_nfields, (dbh->db_nfields * 60) + 29 + strlen(table), strlen(query));
	}

	if (mysql_query(SQLsock, query) == -1) {
		fprintf(stderr, "Error creating table!\n");
		fprintf(stderr, "Detailed report: %s\n", mysql_error(SQLsock));
                close(dbh->db_fd);
                free(dbh);
		free(query);
                mysql_close(SQLsock);
                exit(1);
	}

	free(query);
}

void do_inserts(MYSQL *SQLsock, char *table, dbhead *dbh)
{
  int		result, i, j, nc = 0, h;
  field		*fields;
  char		*query, *vals, *vpos, *pos;
  char		str[257], *cvt = NULL, *s;
  u_long	val_len = 0;
  char		*datafile = NULL;
  FILE		*fconv, *tempfile = NULL;

  if (verbose > 1) {
    printf("Inserting records\n");
  }

  if (convert != NULL)    /* If specified conversion table */
  {
    if ( (fconv = fopen (convert, "rt")) == NULL )
      fprintf (stderr, "Cannot open convert file '%s'.\n", convert);
    else
    {
      nc = atoi (fgets (str, 256, fconv));
      if (verbose > 1)
        printf ("Using conversion table '%s' with %d entries\n", convert, nc);
      if ( (cvt = (char *) malloc (nc*2 + 1)) == NULL )
      {
        fprintf(stderr, "Memory allocation error in do_inserts (cvt)\n");
        mysql_close(SQLsock);
        close(dbh->db_fd);
        free(dbh);
        exit(1);
      }
      for (i = 0, fgets (str, 256, fconv); (i < nc * 2) && (str != NULL); i++)
      {
        cvt[i++] = atoi (strtok (str, " \t"));
        cvt[i] = atoi (strtok (NULL, " \t"));
        fgets (str, 256, fconv);
      }
      cvt[i] = '\0';
    }
  }

  for ( i = 0 ; i < dbh->db_nfields ; i++ ) {
    val_len += dbh->db_fields[i].db_flen*2 + 3;
  }

  if (!(query = (char *)malloc(26 + strlen(table) + val_len))) {
    fprintf(stderr,
	    "Memory allocation error in function do_inserts (query)\n");
    mysql_close(SQLsock);
    close(dbh->db_fd);
    free(dbh);
    exit(1);
  }
  if (!(vals = (char *)malloc(val_len))) {
    fprintf(stderr,
	    "Memory allocation error in function do_inserts (vals)\n");
    mysql_close(SQLsock);
    close(dbh->db_fd);
    free(dbh);
    exit(1);
  }
  if (!quick)
  {
    sprintf(query, "INSERT INTO %s VALUES (",table);
  }
  else  /* if specified -q create file for 'LOAD DATA' */
  {
    datafile = tempnam ("/tmp", "d2my");
    tempfile = fopen (datafile, "wt");
    if (tempfile == NULL || datafile == NULL)
    {
      fprintf (stderr, "Cannot open file '%s' for writing\n", datafile);
      return;
    }
    query[0] = '\0';
  }
  vals=query+strlen(query);

  for ( i = 0; i < dbh->db_records; i++)
  {
    if ((fields = dbf_build_record(dbh)) != (field *)DBF_ERROR)
    {
      result = dbf_get_record(dbh, fields,  i);
      if (result == DBF_VALID) {
	vpos=vals;
	for (h = 0; h < dbh->db_nfields; h++) {
	  /* if length of fieldname==0, skip it */
	  if (!strlen(fields[h].db_name))
	    continue;
	  if (vpos != vals)
	    *vpos++= ',';
	  if (fields[h].db_type != 'N' || quick)
	    *vpos++= '\'';
	  if (trim && fields[h].db_type != 'N') /* trim leading spaces */
	  {
	    for (pos = fields[h].db_contents; isspace(*pos) && (*pos != '\0');
                                                                        pos++);
	    memmove (fields[h].db_contents, pos, strlen (pos) + 1);
	  }  
	  if ( (nc > 0) && (fields[h].db_type == 'C'))
	  {
	    for (j = 0; fields[h].db_contents[j] != '\0'; j++)
	    {
	      if ( (s = strchr (cvt, fields[h].db_contents[j])) != NULL )
	        if ( (s - cvt) % 2 == 0 )
	          fields[h].db_contents[j] = s[1];
	    }
	  }
	  if (upper)
	    strtoupper(fields[h].db_contents);
	  if (lower)
	    strtolower(fields[h].db_contents);
	  for (pos = fields[h].db_contents ; *pos ; pos++)
	  {
	    if (*pos == '\\' || *pos == '\'')
	      *vpos++='\\';
	    *vpos++= *pos;
	  }
	  if (fields[h].db_type != 'N' && !quick)
	    *vpos++= '\'';
	  else
	  {
	    if (!fields[h].db_contents[0])
	    {					/* Numeric field is null */
	      if (null_fields)
	      {
		strcpy(vpos, "NULL");
		vpos+=4;
	      }
	      else
		*vpos++ = '0';
	    }
	  }
	  if (quick)
	    *vpos++= '\'';
	}
	if (!quick)
	{
	  vpos[0]=')';				/* End of values */
	  vpos[1]=0;				/* End of query */
	}
	else
	  vpos[0] = '\0';  
	if ((verbose > 1) && ((i % 100) == 0)) {
	  printf("Inserting record %d\n", i);
	}

	if (!quick)
	{
	  if (mysql_query(SQLsock, query) == -1) {
	    fprintf(stderr,
	  	    "Error sending INSERT in record %04d\n", i);
	    fprintf(stderr,
		    "Detailed report: %s\n",
		    mysql_error(SQLsock));
	    if (verbose > 1) {
	      fprintf(stderr, "%s", query);
	    }
	  }  
	}
	else
	{
	  fprintf (tempfile, "%s\n", query);
	}
      }
      dbf_free_record(dbh, fields);
    }
  }

  free(query); free (cvt);
  if (quick)
  {
    fclose (tempfile);
    sprintf (query, "LOAD DATA INFILE '%s' REPLACE INTO table %s fields terminated by ',' enclosed by ''''",
             datafile, table);
    if ( verbose > 1 )
    {
      fprintf (stderr, "%s\n", query); 
    }
    if (mysql_query(SQLsock, query) == -1) {
	    fprintf(stderr,
	  	    "Error sending LOAD DATA INFILE from file '%s'\n", datafile);
	    fprintf(stderr,
		    "Detailed report: %s\n",
		    mysql_error(SQLsock));
    }
    if ( unlink (datafile) == -1 )
    {
      fprintf (stderr, "Error while removing temporary file '%s'.\n", datafile);
    }
    free (datafile);
  }  
}


int main(int argc, char **argv)
{
	int 		i;
	MYSQL		*SQLsock,mysql;
	extern int 	optind;
	extern char	*optarg;
	char		*query;
	dbhead		*dbh;

#ifdef __EMX__
      	MY_INIT(argv[0]);
#endif
	primary[0] = '\0';

	while ((i = getopt(argc, argv, "qfFrne:lucvi:h:p:d:t:s:o:")) != EOF) {
		switch (i) {
			case 'f':
				fieldlow=1;
				break;
			case 'F':
			        var_chars=0;
				break;
			case 'r':
				trim = 1;
				break;	
			case 'n':
			        null_fields=1;
				break;
			case 'v':
				verbose++;
				break;
			case 'c':
				create++;
				break;
			case 'l':
				lower=1;
				break;
			case 'u':
				if (lower) {
					usage();
					printf("Can't use -u and -l at the same time!\n");
					exit(1);
				}
				upper=1;
				break;
			case 'e':
				convert = (char *)strdup (optarg);
				break;
			case 'h':
				host = (char *)strdup(optarg);
				break;
			case 'q':
				quick = 1;
				break;
			case 'p':
				strncpy(primary, optarg, 11);
				break;
			case 'd':
				dbase = (char *)strdup(optarg);
				break;
			case 't':
				table = (char *)strdup(optarg);
				break;
			case 'i':
				indexes = (char *)strdup(optarg);
				break;
			case 's':
				subarg = (char *)strdup(optarg);
				break;
			case 'o':
				flist = (char *)strdup(optarg);
				break;
			case ':':
				usage();
				printf("missing argument!\n");
				exit(1);
			case '?':
				usage();
				printf("unknown argument: %s\n", argv[0]);
				exit(1);
			default:
				break;
		}
	}

	argc -= optind;
	argv = &argv[optind];

	if (argc != 1) {
		usage();
		exit(1);
	}

	if (verbose > 1) {
		printf("Opening dbf-file\n");
	}

	if ((dbh = dbf_open(argv[0], O_RDONLY)) == (dbhead *)-1) {
		fprintf(stderr, "Couldn't open xbase-file %s\n", argv[0]);
		exit(1);
	}

	if (verbose) {
		printf("dbf-file: %s, mySQL-dbase: %s, mySQL-table: %s\n", argv[0],
																 dbase,
																 table);
		printf("Number of records: %ld\n", dbh->db_records);
		printf("NAME:\t\tLENGTH:\t\tTYPE:\n");
		printf("-------------------------------------\n");
		for (i = 0; i < dbh->db_nfields ; i++) {
			printf("%-12s\t%7d\t\t%5c\n",dbh->db_fields[i].db_name,
									 dbh->db_fields[i].db_flen,
									 dbh->db_fields[i].db_type);
		}
	}

	if (verbose > 1) {
		printf("Making connection to mySQL-server\n");
	}
        
	if (!(SQLsock = mysql_connect(&mysql,host,NULL,NULL))) {
		fprintf(stderr, "Couldn't get a connection with the ");
		fprintf(stderr, "designated host!\n");
		fprintf(stderr, "Detailed report: %s\n", mysql_error(&mysql));
		close(dbh->db_fd);
		free(dbh);
		exit(1);
	}

	if (verbose > 1) {
		printf("Selecting database '%s'\n", dbase);
	}

	if ((mysql_select_db(SQLsock, dbase)) == -1) {
		fprintf(stderr, "Couldn't select database %s.\n", dbase);
		fprintf(stderr, "Detailed report: %s\n", mysql_error(SQLsock));
		close(dbh->db_fd);
		free(dbh);
		mysql_close(SQLsock);
		exit(1);
	}
/* Substitute field names */
      do_onlyfields(flist, dbh);
      do_substitute(subarg, dbh);

	if (!create) {
		if (!check_table(SQLsock, table)) {
			printf("Table does not exist!\n");
			exit(1);
		}
	} else {
		if (verbose > 1) {
			printf("Dropping original table (if one exists)\n");
		}

		if (!(query = (char *)malloc(12 + strlen(table)))) {
			printf("Memory-allocation error in main (drop)!\n");
			close(dbh->db_fd);
			free(dbh);
			mysql_close(SQLsock);
			exit(1);
		}

		sprintf(query, "DROP TABLE %s", table);
		mysql_query(SQLsock, query);
		free(query);

/* Build a CREATE-clause
*/
		do_create(SQLsock, table, dbh);
	}

/* Build an INSERT-clause
*/
	if (create < 2)
	  do_inserts(SQLsock, table, dbh);

	if (verbose > 1) {
		printf("Closing up....\n");
	}

    close(dbh->db_fd);
    free(dbh);
    mysql_close(SQLsock);
    exit(0);
}
