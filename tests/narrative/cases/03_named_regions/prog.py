# ::code Outer
before()

# ::code Inner
inside()
# ::endcode Inner

after()
# ::endcode Outer


def escape_time(z, c, max_iter):
    # ::code Iteration
    for i in range(max_iter):
        z = z * z + c
    # ::endcode Iteration
    return z
