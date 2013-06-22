#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <math.h>
#include <assert.h>
#include <complex>
#include <fftw3.h>
#include <algorithm>

using namespace std;

int Ngrid = 512;
int Ngrid3 = Ngrid*Ngrid*Ngrid;
int nbin = 50; //for P(k)
double Lbox = 1500;

struct _gal
{
  float x, y, z, vx, vy, vz, weight;
};

double sinc(double x)
{
  if(fabs(x) < 1e-8) return 1.;
  else return sin(x)/x;
}

template <class T>
T* initialize_vector(int dim)
{

  T *x;
  x = new T[dim];

  for(int i=0;i<dim;i++) x[i] = 0.0;

  return x;
}

double* initialize_fft_grid_vectors(double dk)
{

  double *x;
  x = new double[Ngrid];

  for(int i=0;i<Ngrid;i++)
  {
    if(i < Ngrid/2) x[i] = i*dk;
    else x[i] = (i-Ngrid)*dk;
  }

  return x;
}

double* initialize_fft_grid_vectors2()
{

  double *x;
  x = new double[Ngrid];

  for(int i=0;i<Ngrid;i++)
  {
    if(i < Ngrid/2) x[i] = i*i;
    else x[i] = (i-Ngrid)*(i-Ngrid);
  }

  return x;
}

template <class V>
V*** initialize_3D_array(int dim1, int dim2, int dim3)
{

  V ***x;

  x = new V**[dim1];

  for(int i=0;i<dim1;i++)
  {
    x[i] = new V*[dim2];
    for(int j=0;j<dim2;j++)
    {
      x[i][j] = new V[dim3];
      for(int k=0;k<dim3;k++) x[i][j][k] = 0.0;
    }
  }

  return x;
}

int kill_3D_array(double ***x, int dim1, int dim2)
{
  for(int i=0;i<dim1;i++)
  {
    for(int j=0;j<dim2;j++)
    {
      delete[] x[i][j];
    }
    delete[] x[i];
  }
  delete[] x;

  return 0;
}

double calc_mean(double ***x)
{
  double xsum;
  double xmean;

  for(int i=0;i<Ngrid;i++)
  {
    for(int j=0;j<Ngrid;j++)
    {
      for(int k=0;k<Ngrid;k++) xsum += x[i][j][k];
    }
  }

  xmean = xsum/(Ngrid*Ngrid*Ngrid);

  return xmean;
}

int binpk(double ***pk3d, double *kgrid2, double *km, double *pk)
{
//Bin in k...
  cout << "Done with FFT...on to binning using " << nbin << " bins..." << endl;
  double bin_width = log10(Ngrid/2.)/nbin;

  double *size_bin, *tot_lk, *tot_kpk;
  size_bin = initialize_vector<double>(nbin);
  tot_lk = initialize_vector<double>(nbin);
  tot_kpk = initialize_vector<double>(nbin); //stores k*P(k)

//Because our bins only go up to Ngrid/2 we only need to consider
//P(i,j,k) where 0 < i,j,k < Ngrid/2. 
//However, we do need to consider 0 > -i,-j,-k > -Ngrid/2.
//Luckily, P(-i,-j,-k) = P(Ngrid-i, Ngrid-j, Ngrid-k) which corresponds
//to P(i,j,k) for grid/2 <= i,j,k < Ngrid...
//So we can loop in i,j,k from 0 to Ngrid.
  for(int i=0;i<Ngrid;i++)
  {
    for(int j=0;j<Ngrid;j++)
    {
      for(int k=0;k<Ngrid;k++)
      {
        if(i==0 && j==0 && k==0) continue;

        double d2 = kgrid2[i] + kgrid2[j] + kgrid2[k];
//The above is just kx*kx + ky*ky + kz*kz
//Corresponds to:
//i*i+j*j+k*k IF i,j,k<Ngrid/2
//(Ngrid-i)*(Ngrid-i)+(Ngrid-j)*(Ngrid-j)*(Ngrid-k)*(Ngrid-k) IF i,j,k>Ngrid/2
//where the second case represents the -i,-j,-k points of interest.
        double ld = 0.5*log10(d2);
        int bin = floor(ld/bin_width);

        if(bin >= 0 && bin < nbin)
        {
          size_bin[bin]++;
          tot_lk[bin] += ld;
          tot_kpk[bin] += (sqrt(d2) * pk3d[i][j][k]);
        }
      }
    }
  }

  for(int i=0;i<nbin;i++)
  {
    if(size_bin[i] > 0)
    {
      km[i] = pow(10.,tot_lk[i]/size_bin[i]);
      pk[i] = tot_kpk[i]/size_bin[i] / km[i];

      km[i] = km[i] * (2.*M_PI/Lbox);
      pk[i] = pk[i] * pow(Lbox,3);
//      cout << i << ' ' << size_bin[i] << endl;
    }
  }

  delete[] size_bin;
  delete[] tot_lk;
  delete[] tot_kpk;

  return 0;
}

int powspec(double ***delta, double *km, double *pk)
{
//Set up grid values in k...(don't need this for fft but need for binning)
  double *kgrid2;
  kgrid2 = initialize_fft_grid_vectors2();
  double dk = 2.*M_PI/Lbox;
  double *kgrid;
  kgrid = initialize_fft_grid_vectors(dk);

//Do fft to get P(k)...
  double *delta_in;
  int r2csize = Ngrid*Ngrid*(Ngrid+2);
  delta_in = initialize_vector<double>(r2csize);
  int count=0;
  for(int i=0;i<Ngrid;i++)
  {
    for(int j=0;j<Ngrid;j++)
    {
      for(int k=0;k<Ngrid;k++)
      {
        delta_in[count] = delta[i][j][k];
        count++;
//For r2c, array padding (2 elements) at the end of each k-row. So skip 2 if
//k = Ngrid-1 (last element of k-row).
      }
      count+=2;
    }
  }
  assert(count == r2csize);

  cout << "Calculating the power spectrum (FFT-ing)..." << endl;
  fftw_plan plan;
//Do fft in-place!
  plan = fftw_plan_dft_r2c_3d(Ngrid,Ngrid,Ngrid,delta_in,
                              (fftw_complex *)delta_in, FFTW_ESTIMATE);
  fftw_execute(plan);
  fftw_destroy_plan(plan);

  double sinckx,sincky,sinckz,wcic,lcico2;
  lcico2 = 0.5*Lbox/Ngrid;

  double ***pk_3D;
  pk_3D = initialize_3D_array<double>(Ngrid,Ngrid,Ngrid);
  count = 0;
  for(int i=0;i<Ngrid;i++)
  {
    sinckx = sinc(kgrid[i]*lcico2);
    sinckx *= sinckx;
    for(int j=0;j<Ngrid;j++)
    {
      sincky = sinc(kgrid[j]*lcico2);
      sincky *= sincky;
      for(int k=0;k<Ngrid/2+1;k++)
      {
        //for some reason I need to divide by Ngrid**3...this appears to be due
        //to a difference between how the FFT is defined in fftw versus IDL...
        //not sure which one is correct????
        int redex = count;
        int imdex = count+1;
        sinckz = sinc(kgrid[k]*lcico2);
        sinckz *= sinckz;
        wcic = sinckx*sincky*sinckz;

        delta_in[redex] = delta_in[redex]/Ngrid3 /wcic;
        delta_in[imdex] = delta_in[imdex]/Ngrid3 /wcic;
        //hopefully this multiplies by the complex conjugate...
        pk_3D[i][j][k] = delta_in[redex]*delta_in[redex]+
                         + delta_in[imdex]*delta_in[imdex];
//For k=(1,...,Ngrid/2-1) we have:
//delta[ks=(Ngrid-k)] = conjugate(delta[k])
//So:
//P[i][j][ks] = delta[i][j][ks]*conjugate(delta[i][j][ks])
//            = delta[i][j][Ngrid-k]*conjugate(delta[i][j][Ngrid-ks])
//            = conjugate(delta[k])*conjugate(conjugate(delta[k]))
//            = conjugate(delta[k])*delta[k]
//            = P[i][j][k]
//k=0 and k=Ngrid/2 are special cases 
//(but note that P[i][j][Ngrid-Ngrid/2] = P[i][j][Ngrid/2], hence, below we
//only need to treat the k=0 case separately)
        if(k!=0) pk_3D[i][j][Ngrid-k] = pk_3D[i][j][k];
        count+=2;
      }
    }
  }
  delete[] delta_in;
  delete[] kgrid;

  binpk(pk_3D,kgrid2,km,pk);

  delete[] kgrid2;
  kill_3D_array(pk_3D,Ngrid,Ngrid);

  return 0;
}

int crossspec(double ***d1, double ***d2, double *km, double *pk)
{
//Set up grid values in k...(don't need this for fft but need for binning)
  double *kgrid2;
  kgrid2 = initialize_fft_grid_vectors2();
  double dk = 2.*M_PI/Lbox;
  double *kgrid;
  kgrid = initialize_fft_grid_vectors(dk);

//Do fft to get P(k)...
  double *delta_1, *delta_2;
  int r2csize = Ngrid*Ngrid*(Ngrid+2);
  delta_1 = initialize_vector<double>(r2csize);
  delta_2 = initialize_vector<double>(r2csize);
  int count=0;
  for(int i=0;i<Ngrid;i++)
  {
    for(int j=0;j<Ngrid;j++)
    {
      for(int k=0;k<Ngrid;k++)
      {
        delta_1[count] = d1[i][j][k];
        delta_2[count] = d2[i][j][k];
        count++;
      }
      count+=2;
    }
  }
  assert(count == r2csize);

  cout << "Calculating the cross spectrum (FFT-ing)..." << endl;
  fftw_plan plan1;
  plan1 = fftw_plan_dft_r2c_3d(Ngrid,Ngrid,Ngrid,delta_1,
                              (fftw_complex *)delta_1, FFTW_ESTIMATE);
  fftw_execute(plan1);
  fftw_destroy_plan(plan1);

  fftw_plan plan2;
  plan2 = fftw_plan_dft_r2c_3d(Ngrid,Ngrid,Ngrid,delta_2,
                              (fftw_complex *)delta_2, FFTW_ESTIMATE);
  fftw_execute(plan2);
  fftw_destroy_plan(plan2);

  cout << "Done with FFT...on to binning using " << nbin << " bins..." << endl;

  double sinckx,sincky,sinckz,wcic,lcico2;
  lcico2 = 0.5*Lbox/Ngrid;

  double ***pk_3D;
  pk_3D = initialize_3D_array<double>(Ngrid,Ngrid,Ngrid);
  count = 0;
  for(int i=0;i<Ngrid;i++)
  {
    sinckx = sinc(kgrid[i]*lcico2);
    sinckx *= sinckx;
    for(int j=0;j<Ngrid;j++)
    {
      sincky = sinc(kgrid[j]*lcico2);
      sincky *= sincky;
      for(int k=0;k<Ngrid/2+1;k++)
      {
        //for some reason I need to divide by Ngrid**3...this appears to be due
        //to a difference between how the FFT is defined in fftw versus IDL...
        //not sure which one is correct????
        int redex = count;
        int imdex = count+1;
        sinckz = sinc(kgrid[k]*lcico2);
        sinckz *= sinckz;
        wcic = sinckx*sincky*sinckz;

        delta_1[redex] = delta_1[redex]/Ngrid3 /wcic;
        delta_1[imdex] = delta_1[imdex]/Ngrid3 /wcic;
        delta_2[redex] = delta_2[redex]/Ngrid3 /wcic;
        delta_2[imdex] = delta_2[imdex]/Ngrid3 /wcic;
        //hopefully this multiplies by the complex conjugate...
        pk_3D[i][j][k] = delta_1[redex]*delta_2[redex]+
                         + delta_1[imdex]*delta_2[imdex];
        if(k!=0) pk_3D[i][j][Ngrid-k] = pk_3D[i][j][k];
        count+=2;
      }
    }
  }
  delete[] delta_1;
  delete[] delta_2;
  delete[] kgrid;

  binpk(pk_3D,kgrid2,km,pk);

  delete[] kgrid2;
  kill_3D_array(pk_3D,Ngrid,Ngrid);

  return 0;
}

int cic_mass(_gal *p, int N, double ***rho)
{
  double lside = Ngrid/Lbox;

  for(int i=0;i<N;i++)
  {
//Because the box is periodic, the Ngrid+1th grid point is the 0th grid point!
//That's why Ngrid == Ncell!
    double dd1 = p[i].x*lside;
    double dd2 = p[i].y*lside;
    double dd3 = p[i].z*lside;
    int i1 = floor(dd1);
    int i2 = floor(dd2);
    int i3 = floor(dd3);
    int j1 = i1 + 1;
    int j2 = i2 + 1;
    int j3 = i3 + 1;
    dd1 = dd1 - i1;
    dd2 = dd2 - i2;
    dd3 = dd3 - i3;
    double de1 = 1. - dd1;
    double de2 = 1. - dd2;
    double de3 = 1. - dd3;
//If the upper bound of the cell is the Ngrid+1th grid point, then set it back
//to the 0th grid point!
    if(i1 == Ngrid){i1 = 0; j1 = 1;}
    if(i2 == Ngrid){i2 = 0; j2 = 1;}
    if(i3 == Ngrid){i3 = 0; j3 = 1;}
    if(j1 == Ngrid) j1 = 0;
    if(j2 == Ngrid) j2 = 0;
    if(j3 == Ngrid) j3 = 0;
    assert(i1 >= 0 && i1 < Ngrid &&
           i2 >= 0 && i2 < Ngrid &&
           i3 >= 0 && i3 < Ngrid &&
           j1 >= 0 && j1 < Ngrid &&
           j2 >= 0 && j2 < Ngrid &&
           j3 >= 0 && j3 < Ngrid);
/*    if(i1 < 0 || i1 > Ngrid ||
           i2 < 0 || i2 > Ngrid || 
           i3 < 0 || i3 > Ngrid ||
           j1 < 0 || j1 > Ngrid || 
           j2 < 0 || j2 > Ngrid ||
           j3 < 0 || j3 > Ngrid){
    cout << i1 << ' ' << i2 << ' ' << i3 << endl;
    cout << j1 << ' ' << j2 << ' ' << j3 << endl;
    cout << i << ' ' << p[i].x << ' ' << p[i].y << ' ' << p[i].z << endl; }*/

//    rho[i1][i2][i3] += 1;
    rho[i1][i2][i3] += de1*de2*de3;
    rho[j1][i2][i3] += dd1*de2*de3;
    rho[i1][j2][i3] += de1*dd2*de3;
    rho[j1][j2][i3] += dd1*dd2*de3;
    rho[i1][i2][j3] += de1*de2*dd3;
    rho[j1][i2][j3] += dd1*de2*dd3;
    rho[i1][j2][j3] += de1*dd2*dd3;
    rho[j1][j2][j3] += dd1*dd2*dd3;
  }
/*  cout << rho[0][0][0] << endl;
  cout << rho[Ncell][Ncell][Ncell] << endl;
  exit(0);*/

  return 0;
}

int calc_delta(double ***delta, _gal *p, int N)
{
  double ***rho;
  rho = initialize_3D_array<double>(Ngrid,Ngrid,Ngrid);
  cic_mass(p, N, rho);
  for(int i=0;i<Ngrid;i++)  cout << i << ' '  << rho[i][i][i] << endl;

  double rhomean;
  rhomean = calc_mean(rho);
  cout << "Mean galaxy rho: " << rhomean << endl;

  for(int i=0;i<Ngrid;i++)
  {
    for(int j=0;j<Ngrid;j++)
    {
      for(int k=0;k<Ngrid;k++)
      {
        delta[i][j][k] = rho[i][j][k]/rhomean-1.;
      }
    }
 //   cout << i << ' ' << delta[i][i][i] << ' ' << rho[i][i][i] << endl;
  }
//  cout << rho[0][0][1] << ' ' << rho[0][1][1] << ' ' << rho[1][0][1] << ' ' << rho[1][1][0] << ' ' <<  rho[0][1][0] << ' ' << rho[1][0][0] << endl;
  kill_3D_array(rho,Ngrid,Ngrid);

  return 0;
}

int get_num_lines(string infile)
{
  int nline=0;
  string line;
  ifstream ff;

  ff.open(infile.c_str(), ios::in);
  if(ff.is_open())
  {
    while(!ff.eof())
    {
      getline(ff,line);
      nline++;
    }
    ff.close();
  }

//It always counts one extra line for some reason 
//(only EOF indicator on last line??)
  return nline-1;
}

_gal* read_file(string fname, int *N)
{
  //Read in galaxies
  ifstream infile;

  cout << "Reading " << fname << endl;
  *N = get_num_lines(fname);
  cout << "Number of objects = " << *N << endl;

  _gal *g;
  g = new _gal[*N];
  infile.open( fname.c_str(), ios::in );

  for(int i=0;i<*N;i++)
  {
    string line;

    getline(infile,line);
    stringstream ss(line);
    ss >> g[i].x >> g[i].y >> g[i].z;
    g[i].weight = 1.;
  }

  return g;
}

int main(int argc, char* argv[])
{
  int ii=1;
  int zflag = 0;
  string infile;
  while(ii < argc)
  {
    string arg = argv[ii];
    if(arg == "-d")
    {
      ii++;
      infile = argv[ii];
      ii++;
    }
  }

  _gal *gal;
  int Ngal;
  gal = read_file(infile+".txt",&Ngal);
  cout << "Galaxies:" << endl;
  cout << gal[0].x << ' ' << gal[0].y << ' ' << gal[0].z << endl;

  double ***delta;
  delta = initialize_3D_array<double>(Ngrid,Ngrid,Ngrid);
  calc_delta(delta, gal, Ngal);

  double *kk, *pp;
  kk = initialize_vector<double>(nbin);
  pp = initialize_vector<double>(nbin);

  powspec(delta,kk,pp);

  double *ck, *cp;
  ck = initialize_vector<double>(nbin);
  cp = initialize_vector<double>(nbin);
  crossspec(delta,delta,ck,cp);
  for(int i=0;i<nbin;i++) cout << ck[i] << ' ' << cp[i] << endl;

  kill_3D_array(delta,Ngrid,Ngrid);
  
  stringstream ss;
  ss << infile << "_Ngrid" << Ngrid << ".pk";
  ofstream out;
  out.open(ss.str().c_str(),ios::out);
  out.precision(8);
  for(int i=0;i<nbin;i++) out << kk[i] << ' ' << pp[i] << endl;
  out.close();

  delete[] kk;
  delete[] pp;
  delete[] ck;
  delete[] cp;
 
  return 0;
}
