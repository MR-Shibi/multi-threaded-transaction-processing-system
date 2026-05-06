#include <stdio.h>

int main() {
    int txn_id = -1, user_id = -1;
    double amount = 0;
    char type[16] = "";
    
    const char* str = "SELECT 1,4,550.00,'TRANSFER','PAID'";
    int ret = sscanf(str, "SELECT %d,%d,%lf,'%15[^']'", &txn_id, &user_id, &amount, type);
    printf("ret=%d txn_id=%d user_id=%d amount=%f type=%s\n", ret, txn_id, user_id, amount, type);
    return 0;
}
