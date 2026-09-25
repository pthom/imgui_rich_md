r"""::md In a string
# Introduction

Some *prose*.
"""

R'''::md Single quotes
In a single-quoted string, with a prefix.
'''


def f():
    """::md In a docstring
    The docstring's indentation is removed:

        an indented block stays indented.
    """
    return 1


# ::md In line comments
# # A title, not a comment
#
# - a list
#   - nested
# ::endmd In line comments

x = 1
