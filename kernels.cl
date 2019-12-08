float4 faux_clamp(__read_only image2d_t buf, int2 test, int2 minb, int2 maxb, float4 fallback)
{
    float mixer = any(test < minb) || any(test >= maxb);

    sampler_t sam = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;

    return mix(read_imagef(buf, sam, test), fallback, mixer);
}

__kernel
void blur_image(__read_only image2d_t read_buf, __write_only image2d_t write_buf, int w, int h, int ox, int oy)
{
    sampler_t sam = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;

    int x = get_global_id(0);
    int y = get_global_id(1);

    if(x >= w || y >= h)
        return;

    x += ox;
    y += oy;

    float4 centre = read_imagef(read_buf, sam, (int2){x, y});

    float4 up = faux_clamp(read_buf, (int2){x, y-1}, (int2){ox, oy}, (int2){ox + w, oy + h}, centre);
    float4 left = faux_clamp(read_buf, (int2){x-1, y}, (int2){ox, oy}, (int2){ox + w, oy + h}, centre);
    float4 right = faux_clamp(read_buf, (int2){x+1, y}, (int2){ox, oy}, (int2){ox + w, oy + h}, centre);
    float4 down = faux_clamp(read_buf, (int2){x, y+1}, (int2){ox, oy}, (int2){ox + w, oy + h}, centre);

    float4 out = (left + right + up + down + centre) / 5.f;

    if(x < 0 || y < 0 || x >= get_image_width(write_buf) || y >= get_image_height(write_buf))
        return;

    out = clamp(out, 0.f, 1.f);

    write_imagef(write_buf, (int2){x, y}, out);
}
