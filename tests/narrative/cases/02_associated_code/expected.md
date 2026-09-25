### Escape time
Iterate until $|z| > 2$.

```python
def escape_time(z, c, max_iter):
    for i in range(max_iter):
        z = z * z + c
    return z
```

The prose and the code are transcluded separately:

```python
def square(x):
    return x * x
```

The square of a number:

The cube, from a docstring:

```python
return x * x * x
```

The area of a circle.

```cpp
float Area(float r)
{
    return 3.14159265f * r * r;
}
```
