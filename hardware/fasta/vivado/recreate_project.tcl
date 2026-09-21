# Recreate the final FASTA AXI4-Stream/DMA Vivado design.
# Target: ZC702 (xc7z020clg484-1)
# Tested with Vivado 2025.2.

set script_dir [file dirname [file normalize [info script]]]
set project_dir [file normalize [file join $script_dir build]]
set ip_dir [file normalize [file join $script_dir ip rle_fasta_axis_core_1_1]]

create_project -force fasta_rle_zc702 $project_dir -part xc7z020clg484-1
set_property BOARD_PART xilinx.com:zc702:part0:1.4 [current_project]

# Register the custom packaged FASTA RLE IP.
set_property ip_repo_paths [list $ip_dir] [current_project]
update_ip_catalog

# Recreate the block design.
source [file join $script_dir system_bd.tcl]

validate_bd_design
save_bd_design

puts "FASTA Vivado project recreated successfully."
puts "Project directory: $project_dir"
