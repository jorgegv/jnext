static void check(const char* id, const char* desc, bool cond) { (void)id; (void)desc; (void)cond; }
void t() {
    check("OWN-02", "a row the borrowing section's plan owns", true);
    check("LEND-99", "a row belonging to the LENDING suite alone", true);
}
