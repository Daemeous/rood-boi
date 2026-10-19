#include <stdio.h>
extern unsigned char pc_owner[];
extern int log_[16], n;
extern void (*table[])(int);
void G(void);
void F(int);
unsigned char pc_owner[64];
int main(void)
{
    G();             /* original: 1 2 */
    pc_owner[0] = 1;
    G();             /* owned, from inside the unit: 101 102 */
    table[0](7);     /* owned, through a pointer the unit took: 107 */
    pc_owner[0] = 2;
    F(3);            /* shadow: 203 */
    for (int i = 0; i < n; i++)
        printf("%d ", log_[i]);
    printf("\n");
    return 0;
}
