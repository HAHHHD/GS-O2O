file(REMOVE_RECURSE
  "CMakeFiles/tags"
  "app/StaApp_wrap.cc"
  "app/TclInitVar.cc"
  "liberty/LibertyExprLex.cc"
  "liberty/LibertyExprLex.hh"
  "liberty/LibertyExprParse.cc"
  "liberty/LibertyExprParse.hh"
  "liberty/LibertyLex.cc"
  "liberty/LibertyLex.hh"
  "liberty/LibertyParse.cc"
  "liberty/LibertyParse.hh"
  "parasitics/SpefLex.cc"
  "parasitics/SpefLex.hh"
  "parasitics/SpefParse.cc"
  "parasitics/SpefParse.hh"
  "sdf/SdfLex.cc"
  "sdf/SdfLex.hh"
  "sdf/SdfParse.cc"
  "sdf/SdfParse.hh"
  "verilog/VerilogLex.cc"
  "verilog/VerilogLex.hh"
  "verilog/VerilogParse.cc"
  "verilog/VerilogParse.hh"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/tags.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
