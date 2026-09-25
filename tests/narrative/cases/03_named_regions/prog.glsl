void main()
{
    // ::code Lighting
    float light = max(dot(normal, sun), 0.0);
    // ::endcode
}
