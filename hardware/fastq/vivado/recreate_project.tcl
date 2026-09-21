# Recreate the final FASTQ AXI4-Stream/DMA Vivado design.
# Target: ZC702 (xc7z020clg484-1)
# Tested with Vivado 2025.2.

set script_dir [file dirname [file normalize [info script]]]
set project_dir [file normalize [file join $script_dir build]]
set rtl_file [file normalize [file join $script_dir .. rtl 03_axi_stream rle_fastq_axis_core.v]]

create_project -force fastq_rle_zc702 $project_dir -part xc7z020clg484-1
set_property BOARD_PART xilinx.com:zc702:part0:1.4 [current_project]

# Add the FASTQ RLE core used as a module reference in the block design.
add_files -norecurse $rtl_file
update_compile_order -fileset sources_1

# Recreate the block design.
source [file join $script_dir system_bd.tcl]

validate_bd_design
save_bd_design

puts "FASTQ Vivado project recreated successfully."
puts "Project directory: $project_dir"
