/* Synthetic databases only; same statements and semantics across both engines.
 * No production DB, schema migration, encryption or journal policy changes. */
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks=0;
static void require(int ok,const char *what){++checks;if(!ok){fprintf(stderr,"FAIL %s\n",what);exit(1);}}
static void exec(sqlite3 *db,const char *sql){char *error=0;int rc=sqlite3_exec(db,sql,0,0,&error);if(rc!=SQLITE_OK)fprintf(stderr,"SQL %s\n",error?error:"");sqlite3_free(error);require(rc==SQLITE_OK,"statement succeeds");}
static int scalar(sqlite3 *db,const char *sql){sqlite3_stmt *s=0;require(sqlite3_prepare_v2(db,sql,-1,&s,0)==SQLITE_OK,"prepare scalar");require(sqlite3_step(s)==SQLITE_ROW,"scalar row");int result=sqlite3_column_int(s,0);require(sqlite3_finalize(s)==SQLITE_OK,"finalize scalar");return result;}
int main(int argc,char **argv){
 require(argc==3,"mode and synthetic path required");require(!strcmp(argv[1],"create")||!strcmp(argv[1],"check"),"known mode");
 sqlite3 *db=0;require(sqlite3_open(argv[2],&db)==SQLITE_OK,"open synthetic database");
 if(!strcmp(argv[1],"create")){
  exec(db,"CREATE TABLE notes(id INTEGER PRIMARY KEY,title TEXT NOT NULL,body BLOB NOT NULL,modified INTEGER NOT NULL);CREATE INDEX notes_title ON notes(title);PRAGMA user_version=1;");
  exec(db,"BEGIN IMMEDIATE");sqlite3_stmt *insert=0;require(sqlite3_prepare_v2(db,"INSERT INTO notes VALUES(?1,?2,?3,?4)",-1,&insert,0)==SQLITE_OK,"prepare insert");
  for(int i=0;i<1000;++i){const unsigned char blob[]={0,1,2,255,0,128};char title[96];snprintf(title,sizeof(title),"合成笔记-数学公式-%d",i);
   require(sqlite3_bind_int(insert,1,i+1)==SQLITE_OK,"bind id");require(sqlite3_bind_text(insert,2,title,-1,SQLITE_TRANSIENT)==SQLITE_OK,"bind UTF8 title");require(sqlite3_bind_blob(insert,3,blob,sizeof(blob),SQLITE_TRANSIENT)==SQLITE_OK,"bind blob");require(sqlite3_bind_int(insert,4,i)==SQLITE_OK,"bind modified");require(sqlite3_step(insert)==SQLITE_DONE,"insert row");require(sqlite3_reset(insert)==SQLITE_OK,"reset insert");}
  require(sqlite3_finalize(insert)==SQLITE_OK,"finalize insert");exec(db,"COMMIT");
 }
 require(scalar(db,"SELECT count(*) FROM notes")==1000,"row count retained");require(scalar(db,"SELECT sum(modified) FROM notes")==499500,"integer values retained");
 require(scalar(db,"SELECT count(*) FROM notes WHERE title LIKE '合成笔记-数学公式-%'")==1000,"UTF8 titles retained");
 require(scalar(db,"SELECT count(*) FROM notes WHERE hex(body)='000102FF0080'")==1000,"NUL and binary bytes retained");
 require(scalar(db,"PRAGMA user_version")==1,"schema version unchanged");
 exec(db,"BEGIN IMMEDIATE;UPDATE notes SET title='discarded',body=x'00' WHERE id=1;ROLLBACK;");require(scalar(db,"SELECT count(*) FROM notes WHERE title='合成笔记-数学公式-0' AND hex(body)='000102FF0080'")==1,"rollback restores original bytes");
 exec(db,"BEGIN IMMEDIATE;UPDATE notes SET modified=modified+1 WHERE id=1;UPDATE notes SET modified=modified-1 WHERE id=1;COMMIT;");
 require(scalar(db,"SELECT count(*) FROM notes WHERE modified=0")==1,"committed update preserves expected value");
 sqlite3_stmt *s=0;require(sqlite3_prepare_v2(db,"PRAGMA integrity_check",-1,&s,0)==SQLITE_OK,"prepare integrity");require(sqlite3_step(s)==SQLITE_ROW&&!strcmp((const char*)sqlite3_column_text(s,0),"ok"),"synthetic integrity check");require(sqlite3_finalize(s)==SQLITE_OK,"finalize integrity");require(sqlite3_close(db)==SQLITE_OK,"all statements finalized and database closed");
 printf("SQLITE_UPGRADE_RESULT {\"version\":\"%s\",\"mode\":\"%s\",\"checks\":%d,\"failures\":0,\"syntheticOnly\":true}\n",sqlite3_libversion(),argv[1],checks);return 0;
}
