//**************************************************************************
// vvadd.c
//--------------------------------------------------------------------------
// Hiruna Vishwamith
// UOM
// 10/03/2025
//--------------------------------------------------------------------------


#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define data_t int
//--------------------------------------------------------------------------
// vvadd function

void __attribute__((noinline)) vvadd(int coreid, int ncores, int n, const data_t* x, const data_t* y, data_t* z)
{
   int i;
   const int chunksize = n / ncores;
   const int base = coreid * chunksize;
   const int chunk_end = MIN(base + chunksize, n);
   const int end = (chunk_end + chunksize > n) ? n : chunk_end;

   for (i = base; i < end; i++)
   {
      z[i] = x[i] + y[i];
   }
}
