#include <stdio.h>
#include <string.h>
#include <sqlite3.h> 
#include <sys/shm.h>
#include <sys/stat.h> 

int select_action(char* input, int* price);
int update_insert_city(sqlite3 *db, const char *inputLocation, int price);
void clear_stdin(void);

void clear_stdin(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

int select_action(char* input, int* price)
{
    int selection;
    printf("\nselect action\n");
    while(scanf("%d", &selection) != 1)
    {
        printf("Invalid input!\n");
        clear_stdin();
    }
    clear_stdin();
    switch (selection)
    {
        case 1:
        {
            printf("\nselect location\n");
            fgets(input, 20, stdin);
            size_t len = strlen(input);
            if (len > 0 && input[len - 1] == '\n')
            {
                input[len - 1] = '\0';
            }
            printf("\nselect price\n");
            while(scanf("%d", price) != 1)
            {
                printf("Invalid input!\n");
                clear_stdin();
            }
            break;
        }
        // case 2:
        // break;
        // case 3:
        // break;
        default:
            return 1;
    } 
    return 0;
}

int update_insert_city(sqlite3 *db, const char *inputLocation, int price)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql = "INSERT INTO cities (Name, Price) VALUES (?, ?)"
        " ON CONFLICT(Name) DO UPDATE SET Price = EXCLUDED.Price;";

    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

    sqlite3_bind_text(stmt, 1, inputLocation, -1, SQLITE_TRANSIENT);

    sqlite3_bind_int(stmt, 2, price);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return 0;
}

int main(void) {
    sqlite3 *db;
    char *err_msg = 0;
    char inputLocation[20] = {0};
    int price = 0;
    
    
    int rc = sqlite3_open("test.db", &db);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    
    const char *sql = "CREATE TABLE IF NOT EXISTS cities(Id INTEGER PRIMARY KEY, Name TEXT UNIQUE, Price INT);";
    const char *sql2 = "CREATE TABLE IF NOT EXISTS customers(Name TEXT, pay INT, location TEXT, Time DATETIME DEFAULT CURRENT_TIMESTAMP);"
                "INSERT INTO customers(Name, pay, location) VALUES('Jay', 100, 'jerusalem');";
    sqlite3_exec(db, sql, 0, 0, &err_msg);
    sqlite3_exec(db, sql2, 0, 0, &err_msg);

    
    printf("Table created and data inserted successfully!\n");

    select_action(inputLocation, &price);
    update_insert_city(db, inputLocation, price);


    sqlite3_close(db);
    
    return 0;
}