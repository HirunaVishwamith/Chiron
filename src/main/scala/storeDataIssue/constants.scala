package storeDataIssue

object constants {
    val prfAddrWidth    =   6
    //val branchMaskWidth =   4
    val newBranchMaskWidth= 5  //leon coherency

    val fifo_width      = prfAddrWidth + newBranchMaskWidth  //leon coherency
    val fifo_depth      = 16 
}
