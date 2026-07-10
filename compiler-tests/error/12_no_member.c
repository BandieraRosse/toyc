/* EXPECT_ERR: no member named 'z' */
struct S { int x; int y; };
int main() {
    struct S s;
    return s.z;
}
