// by http://www.aholme.co.uk/GPS/SRC/2013/C++/solve.cpp

class Trilateration
{
  private:
    static double TimeFromEpoch(double t, double t_ref)
    {
      t-= t_ref;
      if      (t> 302400) t -= 604800;
      else if (t<-302400) t += 604800;
      return t;
    }

    static double GetClockCorrection(double t, eph_t *eph) {

         // Time from ephemeris reference epoch
        double t_k = TimeFromEpoch(t, eph->toes);

        // Eccentric Anomaly
        double E_k = EccentricAnomaly(t_k);

        // Relativistic correction
        double t_R = F*eph->e*sqrtA*sin(E_k);

        // Time from clock correction epoch
        t = TimeFromEpoch(t, eph->toc.time);

        // 20.3.3.3.3.1 User Algorithm for SV Clock Correction
        // TODO: support group delay params not only for gps
        return eph->f0
             + eph->f1 * pow(t, 1)
             + eph->f2 * pow(t, 2) + t_R - eph->tgd[0];
    }

  public:
    // solves trilateration via. taylor series approximations without using determinant equation
    // does not apply clock correction
    // require satellite position array with at least 3 sats(does not check for data availability)
    // writes x,y,z ecef coords to pointers and clock bias to t_bias pointer
    static int solve_trilat(std::vector <sat_pos> *sp, double *x_n, double *y_n, double *z_n, double *t_bias) {
        int i, j, r, c;

        int n_sats = sp->size();
        if(n_sats>3)
        {

          double t_tx[n_sats]; // Clock replicas in seconds since start of week

          double x_sv[n_sats],
                 y_sv[n_sats],
                 z_sv[n_sats];

          double t_pc;  // Uncorrected system time when clock replica snapshots taken
          double t_rx;  // Corrected GPS time

          double dPR[n_sats]; // Pseudo range error

          double jac[n_sats][4], ma[4][4], mb[4][4], mc[4][n_sats], md[4];

          double weight[n_sats];

          *x_n = *y_n = *z_n = *t_bias = t_pc = 0;

          for (i=0; i<n_sats; i++) {

              if(full_eph_avail((*sp)[i].eph)){
                // weight[i] = (*sp)[i].SNR;
                weight[i] = 1; // for debugging

                // Un-corrected time of transmission
                t_tx[i] = (*sp)[i].eph.toe.time;

                // Clock correction already done by ublox receiver
                // t_tx[i] -= GetClockCorrection(t_tx[i], &(*sp)[i].eph);

                gtime_t t_tx_;
                t_tx_.time = t_tx[i];

                eph2pos(t_tx_, &(*sp)[i].eph,  &(*sp)[i]);

                x_sv[i] = (*sp)[i].pos[0]+i;
                y_sv[i] = (*sp)[i].pos[1]+i;
                z_sv[i] = (*sp)[i].pos[2]+i;

                t_pc += t_tx[i];
              }
          }

          // Approximate starting value for receiver clock
          t_pc = t_pc/n_sats + 75e-3;

          // Iterate to user xyzt solution using Taylor Series expansion:
          for(j=0; j<MAX_ITER; j++) {
              // NextTask();

              t_rx = t_pc - *t_bias;

              for (i=0; i<n_sats; i++) {
                if(full_eph_avail((*sp)[i].eph))
                {
                  // Convert SV position to ECI coords (20.3.3.4.3.3.2)
                  double theta = (t_tx[i] - t_rx) * OMEGA_E;

                  double x_sv_eci = x_sv[i]*cos(theta) - y_sv[i]*sin(theta);
                  double y_sv_eci = x_sv[i]*sin(theta) + y_sv[i]*cos(theta);
                  double z_sv_eci = z_sv[i];

                  // Geometric range (20.3.3.4.3.4)
                  double gr = sqrt(pow(*x_n - x_sv_eci, 2) +
                                   pow(*y_n - y_sv_eci, 2) +
                                   pow(*z_n - z_sv_eci, 2));

                  // dPR[i] = C*(t_rx - t_tx[i]) - gr;
                  dPR[i] = (*sp)[i].pseudo_range_corrected - gr;

                  jac[i][0] = (*x_n - x_sv_eci) / gr;
                  jac[i][1] = (*y_n - y_sv_eci) / gr;
                  jac[i][2] = (*z_n - z_sv_eci) / gr;
                  jac[i][3] = C;
                }
              }

              // here begins the taylor series approximations

              // ma = transpose(H) * W * H
              for (r=0; r<4; r++)
                  for (c=0; c<4; c++) {
                  ma[r][c] = 0;
                  for (i=0; i<n_sats; i++) ma[r][c] += jac[i][r]*weight[i]*jac[i][c];
              }

              // mc = inverse(transpose(H)*W*H) * transpose(H)
              for (r=0; r<4; r++)
                  for (c=0; c<n_sats; c++) {
                  mc[r][c] = 0;
                  for (i=0; i<4; i++) mc[r][c] += mb[r][i]*jac[c][i];
              }
              // md = inverse(transpose(H)*W*H) * transpose(H) * W * dPR
              for (r=0; r<4; r++) {
                  md[r] = 0;
                  for (i=0; i<n_sats; i++) md[r] += mc[r][i]*weight[i]*dPR[i];
              }

              double dx = md[0];
              double dy = md[1];
              double dz = md[2];
              double dt = md[3];

              double err_mag = sqrt(dx*dx + dy*dy + dz*dz);

              // printf("%14g%14g%14g%14g%14g\n", err_mag, t_bias, x_n, y_n, z_n);

              if (err_mag<1.0) break;

              *x_n    += dx;
              *y_n    += dy;
              *z_n    += dz;
              *t_bias += dt;
          }
        }

        // UserStat(STAT_TIME, t_rx);
        return j;
    }
};
