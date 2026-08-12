package Backend.storeDataIssue

import common.configuration

object constants {
    val prfAddrWidth     = configuration.prfAddrWidth
    val newBranchMaskWidth = configuration.newBranchMaskWidth
    val robAddrWidth     = configuration.robAddrWidth

    val fifo_width      = prfAddrWidth + newBranchMaskWidth
    val fifo_depth      = 1 << robAddrWidth
}
