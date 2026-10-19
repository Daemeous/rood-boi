extern int log_[16], n;
void F__owned(int a) { log_[n++] = 100 + a; }
void F__shadow(int a) { log_[n++] = 200 + a; }
