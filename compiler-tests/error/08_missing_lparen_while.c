/* EXPECT_ERR: expected '(' after 'while' */
int main() {
    while x > 0 { return 1; }
    return 0;
}
