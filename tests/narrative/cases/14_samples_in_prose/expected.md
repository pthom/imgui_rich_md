A section is written like this:

```text
::md Name
Some prose.
::code
code...
::endcode
```

The sample above is prose.

The square of a number, with a sample of its own annotations:

```python
# ::code
# ::endcode
::code
```

```python
def square(x: float) -> float:
    return x * x
```

Tildes fence a sample too:

~~~cpp
// ::md Name
// ::endmd
~~~

```cpp
int Square(int x) { return x * x; }
```
