/* EXPECT_ERR: unrecognized character */
int main() {
    return 0;
}
/* Note: the above compiles fine; this test checks that @ triggers an error */
int x = @;
