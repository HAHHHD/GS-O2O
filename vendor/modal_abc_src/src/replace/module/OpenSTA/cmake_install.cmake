# Install script for directory: /Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/opt/homebrew/opt/llvm/bin/llvm-objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/app/sta")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sta" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sta")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" -u -r "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sta")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  include("/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/CMakeFiles/sta.dir/install-cxx-module-bmi-Debug.cmake" OPTIONAL)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/app/libOpenSTA.a")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libOpenSTA.a" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libOpenSTA.a")
    execute_process(COMMAND "/opt/homebrew/opt/llvm/bin/llvm-ranlib" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/libOpenSTA.a")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  include("/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/CMakeFiles/OpenSTA.dir/install-cxx-module-bmi-Debug.cmake" OPTIONAL)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/app/StaMain.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/dcalc/ArcDelayCalc.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/dcalc/Arnoldi.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/dcalc/ArnoldiDelayCalc.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/dcalc/ArnoldiReduce.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/dcalc/DelayCalc.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/dcalc/DcalcAnalysisPt.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/dcalc/DmpCeff.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/dcalc/DmpDelayCalc.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/dcalc/GraphDelayCalc.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/dcalc/GraphDelayCalc1.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/dcalc/LumpedCapDelayCalc.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/dcalc/NetCaps.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/dcalc/RCDelayCalc.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/dcalc/SimpleRCDelayCalc.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/dcalc/UnitDelayCalc.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/graph/Delay.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/graph/DelayFloat.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/graph/DelayNormal1.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/graph/DelayNormal2.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/graph/Graph.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/graph/GraphClass.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/graph/GraphCmp.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/EquivCells.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/FuncExpr.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/InternalPower.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/LeakagePower.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/Liberty.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/LibertyBuilder.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/LibertyClass.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/LibertyParser.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/LibertyReader.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/LibertyReaderPvt.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/LinearModel.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/Sequential.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/TableModel.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/TimingArc.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/TimingModel.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/TimingRole.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/Transition.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/Units.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/liberty/Wireload.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/network/ConcreteLibrary.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/network/ConcreteNetwork.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/network/HpinDrvrLoad.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/network/MakeConcreteNetwork.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/network/Network.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/network/NetworkClass.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/network/NetworkCmp.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/network/ParseBus.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/network/PortDirection.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/network/SdcNetwork.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/network/VerilogNamespace.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/parasitics/ConcreteParasitics.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/parasitics/ConcreteParasiticsPvt.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/parasitics/EstimateParasitics.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/parasitics/MakeConcreteParasitics.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/parasitics/NullParasitics.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/parasitics/Parasitics.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/parasitics/ParasiticsClass.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/parasitics/ReduceParasitics.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/parasitics/SpefNamespace.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/parasitics/SpefReader.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/Clock.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/ClockGatingCheck.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/ClockGroups.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/ClockInsertion.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/ClockLatency.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/CycleAccting.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/DataCheck.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/DeratingFactors.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/DisabledPorts.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/ExceptionPath.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/InputDrive.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/MinMaxValues.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/PinPair.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/PortDelay.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/PortExtCap.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/RiseFallMinMax.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/RiseFallValues.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/Sdc.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/SdcClass.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/SdcCmdComment.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/WriteSdc.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdc/WriteSdcPvt.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdf/ReportAnnotation.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdf/Sdf.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdf/SdfReader.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/sdf/SdfWriter.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/Bfs.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/CheckMaxSkews.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/CheckMinPeriods.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/CheckMinPulseWidths.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/CheckSlewLimits.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/CheckTiming.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/ClkInfo.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/ClkSkew.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/Corner.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/Crpr.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/FindRegister.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/GatedClk.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/Genclks.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/Latches.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/Levelize.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/Path.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/PathAnalysisPt.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/PathEnd.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/PathEnum.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/PathEnumed.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/PathExpanded.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/PathRef.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/PathGroup.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/PathVertex.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/PathVertexRep.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/Power.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/Property.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/ReportPath.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/Search.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/SearchClass.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/SearchPred.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/Sim.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/Sta.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/StaState.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/Tag.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/TagGroup.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/VertexVisitor.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/VisitPathEnds.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/VisitPathGroupVertices.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/WorstSlack.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/search/WritePathSpice.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/Debug.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/DisallowCopyAssign.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/EnumNameMap.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/Error.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/Fuzzy.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/Hash.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/HashSet.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/Iterator.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/Machine.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/Map.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/MinMax.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/Mutex.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/ObjectIndex.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/PatternMatch.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/Pool.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/Report.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/ReportStd.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/ReportTcl.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/Set.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/StaConfig.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/Stats.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/StringSeq.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/StringSet.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/StringUtil.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/ThreadForEach.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/TokenParser.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/UnorderedMap.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/UnorderedSet.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/Vector.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/util/Zlib.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/verilog/VerilogReaderPvt.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/verilog/VerilogReader.hh"
    "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/verilog/VerilogWriter.hh"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/Users/cusgadmin/Documents/code/RePlAce/module/OpenSTA/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
