r"""::md In a string
A section is written like this:

```text
::md Name
Some prose.
::code
code...
::endcode
```

The sample above is prose.
"""


# ::md In comments
# The square of a number, with a sample of its own annotations:
#
# ```python
# # ::code
# # ::endcode
# ::code
# ```
#
# ::code
def square(x: float) -> float:
    return x * x
# ::endcode
