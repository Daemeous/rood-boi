int log_[16], n;
void F(int); /* Calls and address-of retain the public trampoline declaration. */
void F(int a) { log_[n++] = a; }
void G(void) { F(1); F(2); }
void (*table[])(int) = { F };
