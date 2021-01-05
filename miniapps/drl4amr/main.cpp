#include "mfem.hpp"

#include <fstream>
#include <iostream>

using namespace std;
using namespace mfem;

#include "drl4amr.hpp"

int main(int argc, char *argv[])
{
   int P = 2;
   int N = 16;
   double EPS = 1e-2;
   bool periodic = false;
   bool visualization = false;
   const char *base_mesh_save = "";
   constexpr int seed = 0x973afb51;

   OptionsParser args(argc, argv);
   args.AddOption(&N, "-n", "--niter", "Max iterations.");
   args.AddOption(&P, "-o", "--order", "Order.");
   args.AddOption(&EPS, "-e", "--norm", "Loop norm.");
   args.AddOption(&periodic, "-p", "--periodic", "-no-p",
                  "--no-periodic", "Enable or disable periodic mesh.");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.AddOption(&base_mesh_save, "-f", "--file", "File basename.");
   args.Parse();
   if (!args.Good())
   {
      args.PrintUsage(std::cout);
      return 1;
   }
   args.PrintOptions(std::cout);

   Drl4Amr sim(P, visualization, periodic, seed);

   for (int i = 0; (i<N) && sim.GetNorm() > EPS; i++)
   {
      const int e = static_cast<int>(drand48()*sim.GetNE());
      sim.Compute();
      sim.Refine(e);
      sim.GetImage();
      sim.GetIdField();
      sim.GetDepthField();
   }
   if (strlen(base_mesh_save) > 0) { sim.Save(base_mesh_save); }
   return 0;
}