r"""::md Escape
### Escape time
Iterate until $|z| > 2$.
::code
"""
def escape_time(z, c, max_iter):
    for i in range(max_iter):
        z = z * z + c
    return z
# ::endcode

# ::md Square
# The square of a number:
# ::code
def square(x):
    return x * x
# ::endcode
# ::endmd Square


def cube(x):
    """::md Cube
    The cube, from a docstring:
    ::code
    """
    return x * x * x
    # ::endcode
