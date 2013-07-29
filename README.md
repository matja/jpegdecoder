JFIF/JPEG image decoder, made as a learning exercise.

Takes a 24-bit RGB baseline JFIF/JPEG file and displays the decoded image in a new window using SDL.

I have tried to balance portability and performance as well hopefully getting
the decoding correct.

8-bit greyscale, progressive, and arithmetic coding are not supported (yet).

I implemented my own Huffman tree searcher which flattens a Huffman tree into an array and can directly use the Huffman symbol to look up the codeword in constant-time.  It is rather expensive in terms of memory usage - 128kB per huffman table, which rules out implementation on smaller platforms, but is quite acceptable for larger platforms.

My original inverse-DCT was the classic naïve O(n^2) approach which I identified as a huge bottleneck using gprof, and replaced with a row-then-column butterfly DCT (same approach as optimising FFT).

