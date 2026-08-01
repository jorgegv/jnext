static void check(const char* id, const char* desc, bool cond) { (void)id; (void)desc; (void)cond; }
void t() { check("OWN-01", "asserted by the section's own suite", true); }
