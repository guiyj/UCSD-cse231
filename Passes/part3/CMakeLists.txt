add_llvm_library(submission_pt3 MODULE
  LivenessAnalysis.cpp
  MayPointToAnalysis.cpp

  PLUGIN_TOOL
  opt
  )
