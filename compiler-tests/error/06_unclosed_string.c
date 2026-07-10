/* EXPECT_ERR: unterminated string literal */
int main() {
    char *s = "hello
    return 0;
}
