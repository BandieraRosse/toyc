/* 三浮点结构体按值传参，并通过隐藏指针返回。 */
struct vec3 { float x, y, z; };

static struct vec3 transform(struct vec3 v)
{
    struct vec3 r;
    r.x = v.x + 1.0f;
    r.y = v.y * 2.0f;
    r.z = v.z - 3.0f;
    return r;
}

static struct vec3 add(struct vec3 a, struct vec3 b)
{
    struct vec3 r;
    r.x = a.x + b.x;
    r.y = a.y + b.y;
    r.z = a.z + b.z;
    return r;
}

int main(void)
{
    struct vec3 v;
    struct vec3 r;
    struct vec3 b;
    v.x = 2.0f; v.y = 4.0f; v.z = 8.0f;
    r = transform(v);
    if (r.x != 3.0f) return 1;
    if (r.y != 8.0f) return 2;
    if (r.z != 5.0f) return 3;
    b.x = 10.0f; b.y = 20.0f; b.z = 30.0f;
    r = add(v, b);
    if (r.x != 12.0f) return 4;
    if (r.y != 24.0f) return 5;
    if (r.z != 38.0f) return 6;
    return 0;
}
