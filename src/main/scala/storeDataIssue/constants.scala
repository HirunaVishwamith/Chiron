package storeDataIssue

import common.configuration

object constants {
    // Derive from the central configuration so this module tracks the PRF /
    // ROB / branch-mask widths instead of silently capping addresses (a local
    // prfAddrWidth=6 here truncated physical regs >=64 when the PRF grew).
    val prfAddrWidth      = configuration.prfAddrWidth
    val robAddrWidth      = configuration.robAddrWidth
    val newBranchMaskWidth= configuration.newBranchMaskWidth  //leon coherency

    val fifo_width      = prfAddrWidth + newBranchMaskWidth  //leon coherency
    val fifo_depth      = 16
}
