# fastscancount
Fast implementations of the scancount algorithm


Given a set of arrays of integers, we seek to identify 
all values that occur more than 'threshold' times. We do so using the
'scancount' algorithm. It is assumed
that you have fewer than 256 arrays of integers and that the threshold is no larger than 254.

We are effectively providing optimized versions of the following function:

```C++
void scancount(std::vector<std::vector<uint32_t>> &data, std::vector<uint32_t> &out, uint8_t threshold) {
  std::fill(counters.begin(), counters.end(), 0);
  out.clear();
  for (size_t c = 0; c < data.size(); c++) {
    std::vector<uint32_t> &v = data[c];
    for (size_t i = 0; i < v.size(); i++) {
      counters[v[i]]++;
    }
  }
  for (uint32_t i = 0; i < counters.size(); i++) {
    if (counters[i] > threshold)
      out.push_back(i);
  }
}
```

Our optimized versions assume that your arrays are made of sorted integers.

There are two headers, `fastscancount.h` uses plain C++ and should
be portable. It has one main function in the fastscancount namespace.
We always write the result to 'out'.

```C++
void fastscancount(std::vector<std::vector<uint32_t>> &data,
    std::vector<uint32_t> &out, uint8_t threshold)
```

There is another header `fastscancount_avx2.h`
which expects an x64 processor supporting the AVX2 instruction set.  
It has a similar function signature:

```C++
void fastscancount_avx2(std::vector<std::vector<uint32_t>> &data,
    std::vector<uint32_t> &out, uint8_t threshold)
```

The AVX2 version assumes that you have fewer than 128 arrays of integers.

Because this library is made solely of headers, there is no
need for a build system.

## Linux benchmark

If you have bare metal access to a Linux box, you can run cycle-accurate benchmarks.

```
make
./counter
```

Sample output:

```
$ ./counter
Got 2497 hits
optimized cache-sensitive scancount
4.01381 cycles/element
AVX2-based scancount
3.58494 cycles/element
```

## Credit

The AVX2 version was designed and implemented by Travis Downs.
The scalar version was designed and implemented by Daniel Lemire based on ideas by Nathan Kurz,  Travis Downs and others.

## Reference


Owen Kaser, Daniel Lemire, [Compressed bitmap indexes: beyond unions and intersections](https://arxiv.org/abs/1402.4466), Software: Practice and Experience 46 (2), 2016
