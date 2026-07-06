############################################################
# PCIe x1 minimal bring-up constraints (Kintex-7 / XDMA)
############################################################
#set_property LOC GTXE2_CHANNEL_X0Y23 [get_cells -hier -filter {NAME =~ *pipe_lane[0]*gtxe2_channel_i}]
#set_property LOC GTXE2_CHANNEL_X0Y22 [get_cells -hier -filter {NAME =~ *pipe_lane[1]*gtxe2_channel_i}]
#set_property LOC GTXE2_CHANNEL_X0Y21 [get_cells -hier -filter {NAME =~ *pipe_lane[2]*gtxe2_channel_i}]
#set_property LOC GTXE2_CHANNEL_X0Y20 [get_cells -hier -filter {NAME =~ *pipe_lane[3]*gtxe2_channel_i}]

###################### PCIe REFCLK (100MHz diff) ###########
set_property PACKAGE_PIN J8 [get_ports pcie_refclk_clk_p]
set_property PACKAGE_PIN J7 [get_ports pcie_refclk_clk_n]

###################### PCIe PERST# #########################
set_property PACKAGE_PIN Y26 [get_ports pcie_perstn]
set_property IOSTANDARD LVCMOS18 [get_ports pcie_perstn]
# set_property PULLUP true [get_ports {pcie_perstn}]

# PCIe RX lanes




# PCIe TX lanes
set_property LOC GTXE2_CHANNEL_X0Y23 [get_cells {top_i/xdma_0/inst/top_xdma_0_0_pcie2_to_pcie3_wrapper_i/pcie2_ip_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[0].gt_wrapper_i/gtx_channel.gtxe2_channel_i}]
set_property PACKAGE_PIN H5 [get_ports {pci_express_x1_rxn[0]}]
set_property PACKAGE_PIN H6 [get_ports {pci_express_x1_rxp[0]}]
set_property PACKAGE_PIN F1 [get_ports {pci_express_x1_txn[0]}]
set_property PACKAGE_PIN F2 [get_ports {pci_express_x1_txp[0]}]

set_property LOC GTXE2_CHANNEL_X0Y22 [get_cells {top_i/xdma_0/inst/top_xdma_0_0_pcie2_to_pcie3_wrapper_i/pcie2_ip_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[1].gt_wrapper_i/gtx_channel.gtxe2_channel_i}]
set_property PACKAGE_PIN J3 [get_ports {pci_express_x1_rxn[1]}]
set_property PACKAGE_PIN J4 [get_ports {pci_express_x1_rxp[1]}]
set_property PACKAGE_PIN H1 [get_ports {pci_express_x1_txn[1]}]
set_property PACKAGE_PIN H2 [get_ports {pci_express_x1_txp[1]}]

set_property LOC GTXE2_CHANNEL_X0Y21 [get_cells {top_i/xdma_0/inst/top_xdma_0_0_pcie2_to_pcie3_wrapper_i/pcie2_ip_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[2].gt_wrapper_i/gtx_channel.gtxe2_channel_i}]
set_property PACKAGE_PIN K5 [get_ports {pci_express_x1_rxn[2]}]
set_property PACKAGE_PIN K6 [get_ports {pci_express_x1_rxp[2]}]
set_property PACKAGE_PIN K1 [get_ports {pci_express_x1_txn[2]}]
set_property PACKAGE_PIN K2 [get_ports {pci_express_x1_txp[2]}]

set_property LOC GTXE2_CHANNEL_X0Y20 [get_cells {top_i/xdma_0/inst/top_xdma_0_0_pcie2_to_pcie3_wrapper_i/pcie2_ip_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[3].gt_wrapper_i/gtx_channel.gtxe2_channel_i}]
set_property PACKAGE_PIN L3 [get_ports {pci_express_x1_rxn[3]}]
set_property PACKAGE_PIN L4 [get_ports {pci_express_x1_rxp[3]}]
set_property PACKAGE_PIN M1 [get_ports {pci_express_x1_txn[3]}]
set_property PACKAGE_PIN M2 [get_ports {pci_express_x1_txp[3]}]

# user_lnk_up -> LED0 (reuse old board LED pin)
set_property PACKAGE_PIN P30 [get_ports user_lnk_up_0]
set_property IOSTANDARD LVCMOS18 [get_ports user_lnk_up_0]

set_property BITSTREAM.CONFIG.UNUSEDPIN Pullup [current_design];

set_property BITSTREAM.CONFIG.CONFIGRATE 3 [current_design];# Set configuration clock frequency to 26MHz when sync mode is used
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design] ;# Enable bitstream compression, must be enabled otherwise it will be slow
set_property CFGBVS GND [current_design] ;# Configuration bank voltage select ground reference, required
set_property CONFIG_VOLTAGE 1.8 [current_design]; # Used together with the previous line

# No BPI flash / linear flash constraints here: chiron is JTAG-programmed to
# FPGA memory only (`make program`), never flashed — flash-boot on this board
# has a history of getting stuck at boot, so the flash path is intentionally
# not wired up at all rather than merely unused.
set_property IOSTANDARD LVCMOS18 [get_ports bpi_flash_wen]