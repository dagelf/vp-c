#!/bin/bash
# Generate C++ header with embedded web.html

input_file="web.html"
output_file="src/web_html.hpp"

echo "Embedding $input_file into $output_file..."

cat > "$output_file" << 'HEADER'
#ifndef VP_WEB_HTML_HPP
#define VP_WEB_HTML_HPP

namespace vp {

const char* const EMBEDDED_WEB_HTML = R"WEBHTML(
HEADER

cat "$input_file" >> "$output_file"

cat >> "$output_file" << 'FOOTER'
)WEBHTML";

} // namespace vp

#endif // VP_WEB_HTML_HPP
FOOTER

echo "Done! Generated $output_file"
