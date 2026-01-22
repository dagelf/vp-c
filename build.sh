mkdir -p build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make && make test \
  && strip vp \
  && ls -lh vp
