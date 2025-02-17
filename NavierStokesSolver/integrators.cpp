#include <iostream>
#include <armadillo>
#include <vector>
#include <string>

#include <stdexcept>

#include "data.h"
#include "solver.h"
#include "integrators.h"
#include "iterative.h"

//#define CALCULATE_ENERGY
#define PRINT_TIME

//#define CALCULATE_ROM_ERROR

//#define USE_SPECTRAL

template<bool COLLECT_DATA>
arma::Col<double> ExplicitRungeKutta_NS<COLLECT_DATA>::integrate(double finalT, double dt, const arma::Col<double>& initialVel, const arma::Col<double>& initialP, const solver& solver, double collectTime) {

#ifdef CALCULATE_ENERGY
	std::vector<double> kineticEnergy;
#endif

	std::vector<arma::Col<double>> Us;
	std::vector<arma::Col<double>> Fs;

	arma::Col<double> Vo = initialVel;
	arma::Col<double> V;
	arma::Col<double> MV;
	arma::Col<double> phi;

	double nu = solver.nu();
	double t = 0.0;

	int it = 0;
	int itsPerSave = 1250; //1600;//
	int itsPerSnap = 1; // 25; //
	int count = 0;

	arma::SpMat<double> OmInvG = solver.OmInv() * solver.G();
	arma::SpMat<double> nuD = nu * solver.D();

	while (t < finalT) {

		Us.push_back(Vo);

		for (int i = 0; i < m_tableau.s; ++i) {

			V = Vo;

#ifndef USE_SPECTRAL 

			Fs.push_back(solver.OmInv() * (-solver.N(Us[i]) + nuD * Us[i]));

#else

			Fs.push_back(solver.OmInv() * (-solver.N(Us[i]) + nu * solver.spectralDiffusion(Us[i]) ));

#endif

			for (int j = 0; j < (i + 1); ++j) {

				if (i < (m_tableau.s - 1)) {
					V += dt * m_tableau.A[i + 1][j] * Fs[j];
				}
				else {
					V += dt * m_tableau.b[j] * Fs[j];
				}
			}

			MV = solver.M() * V;

			phi = solver.poissonSolve(MV);

			Us.push_back(V - OmInvG * phi);

		}
		
		++it;
		t = t + dt;

		
		if constexpr (Base_Integrator<COLLECT_DATA>::m_collector.COLLECT_DATA) {
			if (it % itsPerSnap == 0) {
				Base_Integrator<COLLECT_DATA>::m_collector.addColumn(Us.back());
				Base_Integrator<COLLECT_DATA>::m_collector.addOperatorColumn(solver.N(Us.back()));
				
				/*
				if (it % itsPerSave == 0) {
					Base_Integrator<COLLECT_DATA>::m_collector.getDataMatrix().save("solution_snapshots_2dturb_" + std::to_string(count + 7), arma::arma_binary);
					Base_Integrator<COLLECT_DATA>::m_collector.getOperatorMatrix().save("operator_snapshots_2dturb_" + std::to_string(count + 7), arma::arma_binary);

					Base_Integrator<COLLECT_DATA>::m_collector.clearData();
					Base_Integrator<COLLECT_DATA>::m_collector.clearOperatorData();

					std::cout << "stability check: " << arma::abs(Us.back()).max() << std::endl;

					++count;
				}
				*/
			}
		}

		if (abs(finalT - t) < (0.01 * dt)) {
			std::cout << t << std::endl;
#ifdef CALCULATE_ENERGY
			arma::Col<double>(kineticEnergy).save("fom_kinetic_energy.txt", arma::raw_ascii);
#endif
			return Us.back();
		}

		if (t > finalT) {
			std::cout << t << std::endl;
#ifdef CALCULATE_ENERGY
			arma::Col<double>(kineticEnergy).save("fom_kinetic_energy.txt", arma::raw_ascii);
#endif
			return Vo;
		}

		Vo = std::move(Us.back());

		Us.clear();
		Fs.clear();

#ifdef CALCULATE_ENERGY
		kineticEnergy.push_back(0.5 * arma::as_scalar(Vo.t() * solver.Om() * Vo));
#endif

		//std::cout << Vo.max() << std::endl;

#ifdef PRINT_TIME
		std::cout << "time: " << t << std::endl;
#endif

	}

#ifdef CALCULATE_ENERGY
	arma::Col<double>(kineticEnergy).save("fom_kinetic_energy.txt", arma::raw_ascii);
#endif

	return Vo;
}

template arma::Col<double> ExplicitRungeKutta_NS<false>::integrate(double finalT, double dt, const arma::Col<double>& initialVel, const arma::Col<double>& initialP, const solver& solver, double collectTime);
template arma::Col<double> ExplicitRungeKutta_NS<true>::integrate(double finalT, double dt, const arma::Col<double>& initialVel, const arma::Col<double>& initialP, const solver& solver, double collectTime);





template<bool COLLECT_DATA>
arma::Col<double> ImplicitRungeKutta_NS<COLLECT_DATA>::integrate(double finalT, double dt, const arma::Col<double>& initialVel, const arma::Col<double>& initialP, const solver& solver, double collectTime) {

#ifdef CALCULATE_ENERGY
	std::vector<double> kineticEnergy;
	
	arma::Mat<double> momentum;

	std::vector<double> divergence;

	const arma::field<cell>& CellsU = solver.getMesh().getCellsU();
	const arma::field<cell>& CellsV = solver.getMesh().getCellsV();

	arma::Col<double> Eu = arma::zeros(solver.getMesh().getNumU() + solver.getMesh().getNumV());
	arma::Col<double> Ev = arma::zeros(solver.getMesh().getNumU() + solver.getMesh().getNumV());

	//setup momentum conserving modes
	for (arma::uword i = solver.getMesh().getStartIndUy(); i < solver.getMesh().getEndIndUy(); ++i) {
		for (arma::uword j = solver.getMesh().getStartIndUx(); j < solver.getMesh().getEndIndUx(); ++j) {
			Eu(CellsU(i, j).vectorIndex) = 1.0;
		}
	}

	for (arma::uword i = solver.getMesh().getStartIndVy(); i < solver.getMesh().getEndIndVy(); ++i) {
		for (arma::uword j = solver.getMesh().getStartIndVx(); j < solver.getMesh().getEndIndVx(); ++j) {
			Ev(CellsV(i, j).vectorIndex) = 1.0;
		}
	}

	arma::Mat<double> E = arma::join_rows(Eu, Ev);

#endif

	arma::uword numU = solver.getMesh().getNumU() + solver.getMesh().getNumV();
	arma::uword numPhi = solver.getMesh().getNumCellsX() * solver.getMesh().getNumCellsY();

	arma::Col<double> Vo = initialVel;

	std::vector <arma::SpMat<double>> aCols;
	arma::SpMat<double> as(m_tableau.s, m_tableau.s);

	arma::Col<double> _col(m_tableau.s);

	for (arma::uword i = 0; i < m_tableau.s; ++i) {

		for (int j = 0; j < m_tableau.s; ++j) {
			_col(j) = m_tableau.A[j][i];

			as(i, j) = m_tableau.A[i][j];
		}

		aCols.push_back(arma::SpMat<double>(_col));

	}

	std::cout << numU << " " << m_tableau.s * (numU + numPhi) << std::endl;

	as = arma::kron(as, arma::speye(numU, numU));

	arma::Col<double> stagesPrev = 100.0 + arma::repmat(arma::Mat<double>(Vo), m_tableau.s, 1).as_col();
	arma::Col<double> stagesNext = arma::repmat(arma::Mat<double>(Vo), m_tableau.s, 1).as_col();

	//does not do bound checking
	auto getStage = [&](const arma::Col<double>& stages, arma::uword stage) {
		return stages.subvec((stage - 1) * numU, stage * numU - 1);
			};

	arma::SpMat<double> Is = arma::speye(m_tableau.s * numU, m_tableau.s * numU);

	arma::SpMat<double> Ms = arma::kron(arma::speye(m_tableau.s, m_tableau.s), solver.M());
	arma::SpMat<double> Gs = arma::kron(arma::speye(m_tableau.s, m_tableau.s), dt * solver.OmInv() * solver.G());
	arma::SpMat<double> empty(Ms.n_rows, Gs.n_cols);

	arma::SpMat<double> MsAndEmpty = arma::join_rows(Ms, empty);

	arma::SpMat<double> dFdu;
	arma::SpMat<double> S;

	arma::Col<double> zeros = arma::zeros(Ms.n_rows + m_tableau.s);
	arma::Col<double> operatorEval(Gs.n_rows);
	arma::Col<double> rhs;

	double nu = solver.nu();
	double t = 0.0;

	arma::SpMat<double> constraints(m_tableau.s, m_tableau.s * (numU + numPhi));

	for (arma::uword i = 0; i < numPhi; ++i) {
		for (arma::uword j = 0; j < m_tableau.s; ++j) {

			constraints(j, m_tableau.s * numU + j * numPhi + i) = 1.0;

		}
	}

	arma::SpMat<double> zerosContraints(m_tableau.s, m_tableau.s);

	arma::Col<double> multipliers = arma::zeros(m_tableau.s);

	arma::Col<double> Phi, prevVo;

	bool init = true;
	arma::Mat<double> stagesNextMat;

	while (t < finalT) {

		//solve...
		do {

			stagesPrev = stagesNext.rows(0, m_tableau.s * numU - 1);

			for (int k = 0; k < m_tableau.s; ++k) {

				dFdu = arma::join_rows(dFdu, arma::kron(aCols[k], dt * solver.OmInv() * (solver.J(getStage(stagesPrev, k + 1)) - nu * solver.D())));

			}

			S = arma::join_rows(Is + dFdu, Gs);
			S = arma::join_cols(S, MsAndEmpty);
			S = arma::join_rows(S, constraints.t());
			S = arma::join_cols(S, arma::join_rows(constraints, zerosContraints));

			dFdu.reset();

			for (int k = 1; k < (m_tableau.s + 1); ++k) {
				operatorEval.subvec((k - 1) * numU, k * numU - 1) = dt * (solver.OmInv() * (-solver.N(getStage(stagesPrev, k)) + solver.J(getStage(stagesPrev, k)) * getStage(stagesPrev, k)));
			}

			rhs = arma::repmat(arma::Mat<double>(Vo), m_tableau.s, 1).as_col() + as * operatorEval;
			rhs = arma::join_cols(rhs, zeros);   //perhaps subtract previous C^T lambda to drive Mu + C^T lambda to zero

			//rhs.rows(0, m_tableau.s * numU + m_tableau.s * numPhi - 1) += constraints.t() * multipliers;

			switch (m_solver) {
			case(LINEAR_SOLVER::DIRECT):

				if (!arma::spsolve(stagesNext, S, rhs)) {
					//arma::Mat<double>(S).save("matrix.txt", arma::raw_ascii);
					throw std::runtime_error("Error in linear solve");
				}

				break;
			case(LINEAR_SOLVER::BICGSTAB):

				if (init) {
					stagesNext = arma::join_cols(stagesNext, zeros);

					init = false;
				}

				iterative_solve(stagesNext, S, rhs, "1e-14", solver_type::BiCGSTAB, precond::ilu);

				break;
			case(LINEAR_SOLVER::GMRES):

				if (init) {
					stagesNext = arma::join_cols(stagesNext, zeros);

					init = false;
				}

				iterative_solve(stagesNext, S, rhs, "1e-14", solver_type::GMRES, precond::ilu);

				break;
			}
			
			//std::cout << "max divergence stage vector: " << (solver.M() * getStage(stagesNext, 1)).max() << std::endl;

			multipliers = stagesNext.rows(stagesNext.n_rows - m_tableau.s, stagesNext.n_rows - 1);

			std::cout << "iteration error: " << arma::norm(stagesNext.rows(0, m_tableau.s * numU - 1) - stagesPrev, 2) << std::endl;

		} while (arma::norm(stagesNext.rows(0, m_tableau.s * numU - 1) - stagesPrev, 2) > 1e-11);// / arma::norm(stagesPrev, 2) > 0.000001);
		//assign to Vo...

		//std::cout << "converged to: " << arma::norm(stagesNext.rows(0, m_tableau.s * numU - 1) - stagesPrev, 2) / arma::norm(stagesPrev, 2) << std::endl;
		//std::cout << "lagrange multiplier: " << multipliers << std::endl;

		if (arma::norm(multipliers, "inf") > 10e-13)
			std::cout << "Lagrange multipliers no longer at machine precision..." << std::endl;

		prevVo = Vo;

		for (int k = 0; k < m_tableau.s; ++k) {
			Vo += dt * m_tableau.b[k] * solver.OmInv() * (-solver.N(getStage(stagesNext, k + 1)) + nu * solver.D() * getStage(stagesNext, k + 1));
		}

		Phi = solver.poissonSolve(solver.M() * Vo);
		
		Vo = Vo - solver.OmInv() * solver.G() * Phi;

		//std::cout << "max divergence solution vector: " << (solver.M() * Vo).max() << std::endl;

		stagesNext = arma::repmat(arma::Mat<double>(Vo), m_tableau.s, 1).as_col();

		init = true;

		if constexpr (Base_Integrator<COLLECT_DATA>::m_collector.COLLECT_DATA) {
			if (t <= collectTime) {
				Base_Integrator<COLLECT_DATA>::m_collector.addColumn(Vo);
				Base_Integrator<COLLECT_DATA>::m_collector.addOperatorColumn(solver.N(Vo));
			}
		}

		t = t + dt;

		if (abs(finalT - t) < (0.01 * dt)) {
			std::cout << t << std::endl;
#ifdef CALCULATE_ENERGY
			arma::Col<double>(kineticEnergy).save("fom_kinetic_energy.txt", arma::raw_ascii);
			momentum.save("fom_momentum.txt", arma::raw_ascii);
			arma::Col<double>(divergence).save("fom_divergence.txt", arma::raw_ascii);
#endif
			return Vo;
		}

		if (t > finalT) {
			std::cout.precision(17);
			std::cout << t << std::endl;
#ifdef CALCULATE_ENERGY
			arma::Col<double>(kineticEnergy).save("fom_kinetic_energy.txt", arma::raw_ascii);
			momentum.save("fom_momentum.txt", arma::raw_ascii);
			arma::Col<double>(divergence).save("fom_divergence.txt", arma::raw_ascii);
#endif
			return prevVo;
		}


#ifdef PRINT_TIME
		std::cout << "time: " << t << std::endl;
#endif

#ifdef CALCULATE_ENERGY
		kineticEnergy.push_back(0.5 * arma::as_scalar(Vo.t() * solver.Om() * Vo));
		arma::Col<double> e = E.t() * solver.Om() * Vo;
		momentum = arma::join_rows(momentum, e);
		divergence.push_back(arma::norm(solver.M() * Vo, "inf"));
#endif

	}

#ifdef CALCULATE_ENERGY
	arma::Col<double>(kineticEnergy).save("fom_kinetic_energy.txt", arma::raw_ascii);
	momentum.save("fom_momentum.txt", arma::raw_ascii);
	arma::Col<double>(divergence).save("fom_divergence.txt", arma::raw_ascii);
#endif

	//arma::Mat<double>(S).save("matrix.txt", arma::raw_ascii);

	return Vo;
}

template arma::Col<double> ImplicitRungeKutta_NS<false>::integrate(double finalT, double dt, const arma::Col<double>& initialVel, const arma::Col<double>& initialP, const solver& solver, double collectTime);
template arma::Col<double> ImplicitRungeKutta_NS<true>::integrate(double finalT, double dt, const arma::Col<double>& initialVel, const arma::Col<double>& initialP, const solver& solver, double collectTime);




//not correct yet pressure has to be taken into account in runge-kutta f^j for FOM 
template<bool COLLECT_DATA>
arma::Col<double> RelaxationRungeKutta_NS<COLLECT_DATA>::integrate(double finalT, double dt, const arma::Col<double>& initialVel, const arma::Col<double>& initialP, const solver& solver, double collectTime) {

#ifdef CALCULATE_ENERGY
	std::vector<double> kineticEnergy;
#endif

	//RRK relaxation parameter
	double gamma = 0.0;
	double gammaDenom = 0.0;
	double gammaNom = 0.0;

	arma::Mat<double> fifj(m_tableau.s, m_tableau.s);

	std::vector<arma::Col<double>> Us;
	std::vector<arma::Col<double>> Fs;

	arma::Col<double> Vo = initialVel;
	arma::Col<double> V;
	arma::Col<double> MV;
	arma::Col<double> phi;

	double nu = solver.nu();
	double t = 0.0;

	while (t < finalT) {

		Us.push_back(Vo);

		for (int i = 0; i < m_tableau.s; ++i) {

			V = Vo;

			Fs.push_back(solver.OmInv() * (-solver.N(Us[i]) + nu * solver.D() * Us[i]));

			for (int j = 0; j < (i + 1); ++j) {

				if (i < (m_tableau.s - 1)) {
					V += dt * m_tableau.A[i + 1][j] * Fs[j];
				}
				else {
					//code is ran 3 times
					for (arma::uword k = 0; k < m_tableau.s; ++k) {
						for (arma::uword l = 0; l < m_tableau.s; ++l) {

							fifj(k, l) = arma::as_scalar(Fs[k].t() * Fs[l]);

						}
					}

					gammaDenom = 0.0;
					gammaNom = 0.0;

					for (int k = 0; k < m_tableau.s; ++k) {
						for (int l = 0; l < m_tableau.s; ++l) {

							gammaNom += m_tableau.b[k] * m_tableau.A[k][l] * fifj(k, l);
							gammaDenom += m_tableau.b[k] * m_tableau.b[l] * fifj(k, l);

						}
					}

					gamma = (abs(gammaDenom) > 1e-17) ? (2.0 * gammaNom) / gammaDenom : 1.0;

					//std::cout << "gamma: " << (2.0 * gammaNom) / gammaDenom << ", denominator: " << gammaDenom << ", numerator: " << gammaNom << std::endl;

					V += gamma * dt * m_tableau.b[j] * Fs[j];
				}
			}

			MV = solver.M() * V;

			phi = solver.poissonSolve(MV);

			Us.push_back(V - solver.OmInv() * solver.G() * phi);

		}

		if constexpr (Base_Integrator<COLLECT_DATA>::m_collector.COLLECT_DATA) {
			if (t <= collectTime) {
				Base_Integrator<COLLECT_DATA>::m_collector.addColumn(Vo);
				Base_Integrator<COLLECT_DATA>::m_collector.addOperatorColumn(solver.N(Vo));
			}
		}

		//can be set to t + gamma * dt to improve accuracy
		t = t + dt;

		if (abs(finalT - t) < (0.01 * dt)) {
			std::cout << t << std::endl;
#ifdef CALCULATE_ENERGY
			arma::Col<double>(kineticEnergy).save("fom_kinetic_energy.txt", arma::raw_ascii);
#endif
			return Us.back();
		}

		if (t > finalT) {
			std::cout << t << std::endl;
#ifdef CALCULATE_ENERGY
			arma::Col<double>(kineticEnergy).save("fom_kinetic_energy.txt", arma::raw_ascii);
#endif
			return Vo;
		}

		Vo = std::move(Us.back());

		Us.clear();
		Fs.clear();

#ifdef CALCULATE_ENERGY
		kineticEnergy.push_back(0.5 * arma::as_scalar(Vo.t() * solver.Om() * Vo));
#endif

		//std::cout << Vo.max() << std::endl;

#ifdef PRINT_TIME
		std::cout << "time: " << t << std::endl;
#endif

	}

#ifdef CALCULATE_ENERGY
	arma::Col<double>(kineticEnergy).save("fom_kinetic_energy.txt", arma::raw_ascii);
#endif

	return Vo;
}

template arma::Col<double> RelaxationRungeKutta_NS<false>::integrate(double finalT, double dt, const arma::Col<double>& initialVel, const arma::Col<double>& initialP, const solver& solver, double collectTime);
template arma::Col<double> RelaxationRungeKutta_NS<true>::integrate(double finalT, double dt, const arma::Col<double>& initialVel, const arma::Col<double>& initialP, const solver& solver, double collectTime);







template<bool COLLECT_DATA>
const dataCollector<COLLECT_DATA>& Base_Integrator<COLLECT_DATA>::getDataCollector() const {
	return m_collector;
}

template const dataCollector<true>& Base_Integrator<true>::getDataCollector() const;
template const dataCollector<false>& Base_Integrator<false>::getDataCollector() const;









template<bool COLLECT_DATA>
arma::Col<double> ExplicitRungeKutta_ROM<COLLECT_DATA>::integrate(double finalT, double dt, const arma::Col<double>& initialA, const arma::Col<double>& initialP, const ROM_Solver& solver, double collectTime) {

#ifdef CALCULATE_ENERGY
	std::vector<double> kineticEnergy;
	std::vector<double> enstrophy;
	double dx, dy;

	dx = dy = solver.getSolver().getMesh().getCellsP()(0, 0).dx;

	arma::Mat<double> vortmat;

	for (int i = 0; i < solver.Psi().n_cols; ++i) {
		vortmat = arma::join_rows(vortmat, solver.getSolver().vorticity(solver.Psi().col(i)));
	}

	vortmat = dx * dy * vortmat.t() * vortmat;

	
	arma::Mat<double> momentum;

	const arma::field<cell>& CellsU = solver.getSolver().getMesh().getCellsU();
	const arma::field<cell>& CellsV = solver.getSolver().getMesh().getCellsV();

	arma::Col<double> Eu = arma::zeros(solver.getSolver().getMesh().getNumU() + solver.getSolver().getMesh().getNumV());
	arma::Col<double> Ev = arma::zeros(solver.getSolver().getMesh().getNumU() + solver.getSolver().getMesh().getNumV());

	//setup momentum conserving modes
	for (arma::uword i = solver.getSolver().getMesh().getStartIndUy(); i < solver.getSolver().getMesh().getEndIndUy(); ++i) {
		for (arma::uword j = solver.getSolver().getMesh().getStartIndUx(); j < solver.getSolver().getMesh().getEndIndUx(); ++j) {
			Eu(CellsU(i, j).vectorIndex) = 1.0;
		}
	}

	for (arma::uword i = solver.getSolver().getMesh().getStartIndVy(); i < solver.getSolver().getMesh().getEndIndVy(); ++i) {
		for (arma::uword j = solver.getSolver().getMesh().getStartIndVx(); j < solver.getSolver().getMesh().getEndIndVx(); ++j) {
			Ev(CellsV(i, j).vectorIndex) = 1.0;
		}
	}

	arma::Mat<double> E = arma::join_rows(Eu, Ev);

	arma::Mat<double> momMat = E.t() * solver.getSolver().Om() * solver.Psi();

	//momMat.cols(2, momMat.n_cols - 1) = arma::Mat<double>(2, momMat.n_cols - 2, arma::fill::zeros);
	
	//std::cout << solver.Psi() << std::endl;
#endif

#ifdef CALCULATE_ROM_ERROR

	arma::Mat<double> fom_snapshots;
	fom_snapshots.load("solution_snapshots_slr");
	std::vector<double> errornorms;
	std::vector<double> idealnorms;
	int numm = solver.getSolver().getMesh().getNumU() + solver.getSolver().getMesh().getNumV();
	arma::uword snapcounter = 0;
	arma::Col<double> _;
	arma::Col<double> __;
#endif

	std::vector<arma::Col<double>> as;
	std::vector<arma::Col<double>> Fs;

	arma::Col<double> ao = initialA;
	arma::Col<double> a;

#ifdef CALCULATE_ENERGY
	kineticEnergy.push_back(0.5 * arma::as_scalar(ao.t() * ao));
	enstrophy.push_back(arma::as_scalar(ao.t() * vortmat * ao));

	momentum = arma::join_rows(momentum, momMat * ao);
#endif

	double nu = solver.nu();
	double t = 0.0;

	while (t < finalT) {

		as.push_back(ao);

		for (int i = 0; i < m_tableau.s; ++i) {

			a = ao;

			//std::cout << "trying" << std::endl;

			//std::cout << solver.Nr(as[i]) << std::endl;

			//std::cout << "trying second" << std::endl;

			Fs.push_back((- solver.Nr(as[i]) + nu * solver.Dr() * as[i]));

			//std::cout << "done trying" << std::endl;

			for (int j = 0; j < (i + 1); ++j) {

				if (i < (m_tableau.s - 1))
					a += dt * m_tableau.A[i + 1][j] * Fs[j];
				else
					a += dt * m_tableau.b[j] * Fs[j];

			}

			as.push_back(a);
		}

		if constexpr (Base_ROM_Integrator<COLLECT_DATA>::m_collector.COLLECT_DATA) {
			if (t <= collectTime) {
				Base_ROM_Integrator<COLLECT_DATA>::m_collector.addColumn(ao);
				Base_ROM_Integrator<COLLECT_DATA>::m_collector.addOperatorColumn(solver.getHyperReduction().N(ao, solver));
				//Base_ROM_Integrator<COLLECT_DATA>::m_collector.addOperatorColumn(solver.getSolver().N(solver.Psi() * ao));
			}
		}

#ifdef CALCULATE_ROM_ERROR

		errornorms.push_back(arma::norm(arma::sqrt(solver.getSolver().Om()) * (solver.Psi() * as.back() - fom_snapshots.col(snapcounter))));
		_ = solver.getSolver().Om() * fom_snapshots.col(snapcounter);
		__ = solver.Psi().t() * _;
		_ = solver.Psi() * __;
		__ = arma::speye(numm, numm) * fom_snapshots.col(snapcounter);
		idealnorms.push_back(arma::norm(arma::sqrt(solver.getSolver().Om()) * (__ - _)));
		++snapcounter;

#endif

		ao = as.back();

#ifdef CALCULATE_ENERGY
		kineticEnergy.push_back(0.5 * arma::as_scalar(ao.t() * ao));
		enstrophy.push_back(arma::as_scalar(ao.t() * vortmat * ao));

		momentum = arma::join_rows(momentum, momMat * ao);
#endif


		as.clear();
		Fs.clear();

		t = t + dt;

		if (abs(finalT - t) < (0.01 * dt)) {
			std::cout << t << std::endl;
#ifdef CALCULATE_ROM_ERROR

			arma::Col<double>(errornorms).save("RK4_error_norm.txt", arma::raw_ascii);
			arma::Col<double>(idealnorms).save("RK4_ideal_norm.txt", arma::raw_ascii);
			
#endif
#ifdef CALCULATE_ENERGY
			arma::Col<double>(kineticEnergy).save("RK4_rom_kinetic_energy_" + std::to_string(solver.getDatasetIndex()) + ".txt", arma::raw_ascii);
			arma::Col<double>(enstrophy).save("RK4_rom_enstrophy_" + std::to_string(solver.getDatasetIndex()) + ".txt", arma::raw_ascii);
			momentum.save("RK4_rom_momentum.txt", arma::raw_ascii);
#endif
			return ao;
		}

		if (t > finalT) {
			std::cout << t << std::endl;
#ifdef CALCULATE_ROM_ERROR

			arma::Col<double>(errornorms).save("RK4_error_norm.txt", arma::raw_ascii);
			arma::Col<double>(idealnorms).save("RK4_ideal_norm.txt", arma::raw_ascii);

#endif
#ifdef CALCULATE_ENERGY
			arma::Col<double>(kineticEnergy).save("RK4_rom_kinetic_energy_" + std::to_string(solver.getDatasetIndex()) + ".txt", arma::raw_ascii);
			arma::Col<double>(enstrophy).save("RK4_rom_enstrophy_" + std::to_string(solver.getDatasetIndex()) + ".txt", arma::raw_ascii);
			momentum.save("RK4_rom_momentum.txt", arma::raw_ascii);
#endif
			return ao;
		}


#ifdef PRINT_TIME
		std::cout << "time: " << t << std::endl;
#endif
	}

#ifdef CALCULATE_ENERGY
	arma::Col<double>(kineticEnergy).save("RK4_rom_kinetic_energy.txt", arma::raw_ascii);
	arma::Col<double>(enstrophy).save("RK4_rom_enstrophy.txt", arma::raw_ascii);
	//momentum.save("RK4_rom_momentum.txt", arma::raw_ascii);
#endif

#ifdef CALCULATE_ROM_ERROR

	arma::Col<double>(errornorms).save("RK4_error_norm.txt", arma::raw_ascii);
	arma::Col<double>(idealnorms).save("RK4_ideal_norm.txt", arma::raw_ascii);

#endif

	return ao;
}

template arma::Col<double> ExplicitRungeKutta_ROM<false>::integrate(double finalT, double dt, const arma::Col<double>& initialA, const arma::Col<double>& initialP, const ROM_Solver& solver, double collectTime);
template arma::Col<double> ExplicitRungeKutta_ROM<true>::integrate(double finalT, double dt, const arma::Col<double>& initialA, const arma::Col<double>& initialP, const ROM_Solver& solver, double collectTime);




template<bool COLLECT_DATA>
arma::Col<double> ImplicitRungeKutta_ROM<COLLECT_DATA>::integrate(double finalT, double dt, const arma::Col<double>& initialA, const arma::Col<double>& initialP, const ROM_Solver& solver, double collectTime) {

#ifdef CALCULATE_ENERGY
	std::vector<double> kineticEnergy;
	/*
	arma::Mat<double> momentum;

	const arma::field<cell>& CellsU = solver.getSolver().getMesh().getCellsU();
	const arma::field<cell>& CellsV = solver.getSolver().getMesh().getCellsV();

	arma::Col<double> Eu = arma::zeros(solver.getSolver().getMesh().getNumU() + solver.getSolver().getMesh().getNumV());
	arma::Col<double> Ev = arma::zeros(solver.getSolver().getMesh().getNumU() + solver.getSolver().getMesh().getNumV());

	//setup momentum conserving modes
	for (arma::uword i = solver.getSolver().getMesh().getStartIndUy(); i < solver.getSolver().getMesh().getEndIndUy(); ++i) {
		for (arma::uword j = solver.getSolver().getMesh().getStartIndUx(); j < solver.getSolver().getMesh().getEndIndUx(); ++j) {
			Eu(CellsU(i, j).vectorIndex) = 1.0;
		}
	}

	for (arma::uword i = solver.getSolver().getMesh().getStartIndVy(); i < solver.getSolver().getMesh().getEndIndVy(); ++i) {
		for (arma::uword j = solver.getSolver().getMesh().getStartIndVx(); j < solver.getSolver().getMesh().getEndIndVx(); ++j) {
			Ev(CellsV(i, j).vectorIndex) = 1.0;
		}
	}

	arma::Mat<double> E = arma::join_rows(Eu, Ev);

	arma::Mat<double> momMat = E.t() * solver.getSolver().Om() * solver.Psi();

	//momMat.cols(2, momMat.n_cols - 1) = arma::Mat<double>(2, momMat.n_cols - 2, arma::fill::zeros);
	*/
#endif
	//we keep using Velocity type names for unknowns to avoid confusion with runge-kutta 'a' constants

	arma::uword numU = initialA.n_rows;

	arma::Col<double> Vo = initialA;

#ifdef CALCULATE_ENERGY
	kineticEnergy.push_back(0.5 * arma::as_scalar(Vo.t() * Vo));
	//momentum = arma::join_rows(momentum, momMat * Vo);
#endif

#ifdef CALCULATE_ROM_ERROR

	arma::Mat<double> fom_snapshots;
	fom_snapshots.load("solution_snapshots_slr");
	std::vector<double> errornorms;
	std::vector<double> idealnorms;
	int numm = solver.getSolver().getMesh().getNumU() + solver.getSolver().getMesh().getNumV();
	arma::uword snapcounter = 0;
	arma::Col<double> _;
	arma::Col<double> __;

#endif

	std::vector <arma::Mat<double>> aCols;
	arma::Mat<double> as(m_tableau.s, m_tableau.s);

	arma::Col<double> _col(m_tableau.s);

	for (arma::uword i = 0; i < m_tableau.s; ++i) {

		for (int j = 0; j < m_tableau.s; ++j) {
			_col(j) = m_tableau.A[j][i];

			as(i, j) = m_tableau.A[i][j];
		}

		aCols.push_back(arma::Mat<double>(_col));

	}

	std::cout << numU << " " << m_tableau.s * numU << std::endl;

	as = arma::kron(as, arma::eye(numU, numU));

	arma::Col<double> stagesPrev = 100.0 + arma::repmat(arma::Mat<double>(Vo), m_tableau.s, 1).as_col();
	arma::Col<double> stagesNext = arma::repmat(arma::Mat<double>(Vo), m_tableau.s, 1).as_col();

	//does not do bound checking
	auto getStage = [&](const arma::Col<double>& stages, arma::uword stage) {
		return stages.subvec((stage - 1) * numU, stage * numU - 1);
	};

	arma::Mat<double> Is = arma::eye(m_tableau.s * numU, m_tableau.s * numU);

	arma::Mat<double> dFdu;
	arma::Mat<double> S;

	arma::Col<double> operatorEval(m_tableau.s * initialA.n_rows);
	arma::Col<double> rhs;

	double nu = solver.nu();
	double t = 0.0;

	arma::Col<double> prevVo;

	bool init = true;
	arma::Mat<double> stagesNextMat;

	int it = 0;

	while (t < finalT) {

		it = 0;

		//solve...
		do {

			stagesPrev = stagesNext;

			for (int k = 0; k < m_tableau.s; ++k) {

				dFdu = arma::join_rows(dFdu, arma::kron(aCols[k], dt * (solver.Jr(getStage(stagesPrev, k + 1)) - nu * solver.Dr())));

			}

			S = Is + dFdu;
			dFdu.reset();

			for (int k = 1; k < (m_tableau.s + 1); ++k) {

				operatorEval.subvec((k - 1) * numU, k * numU - 1) = dt * (-solver.Nr(getStage(stagesPrev, k)) + solver.Jr(getStage(stagesPrev, k)) * getStage(stagesPrev, k));

			}

			rhs = arma::repmat(arma::Mat<double>(Vo), m_tableau.s, 1).as_col() + as * operatorEval;

			switch (m_solver) {
			case(LINEAR_SOLVER::DIRECT):

				if (!arma::solve(stagesNext, S, rhs)) {
					//arma::Mat<double>(S).save("matrix.txt", arma::raw_ascii);
					throw std::runtime_error("Error in linear solve");
				}

				break;
			}

			std::cout << "iteration error: " << arma::norm(stagesNext - stagesPrev, 2) << std::endl;

			++it;

		} while (arma::norm(stagesNext - stagesPrev, 2) > 1e-14 && it < 100);// / arma::norm(stagesPrev, 2) > 0.000001);
		//assign to Vo...

		if (it == 100) {
			std::cout << "max its exceeded..." << std::endl;
#ifdef CALCULATE_ROM_ERROR

			arma::Col<double>(errornorms).save("GL4_error_norm.txt", arma::raw_ascii);
			arma::Col<double>(idealnorms).save("GL4_ideal_norm.txt", arma::raw_ascii);

#endif
			return Vo;
			throw std::runtime_error("exceeded max its");
		}

		prevVo = Vo;

		if constexpr (Base_ROM_Integrator<COLLECT_DATA>::m_collector.COLLECT_DATA) {
			if (t <= collectTime) {
				Base_ROM_Integrator<COLLECT_DATA>::m_collector.addColumn(Vo);
				Base_ROM_Integrator<COLLECT_DATA>::m_collector.addOperatorColumn(solver.Nr(Vo));
			}
		}

#ifdef CALCULATE_ROM_ERROR

		errornorms.push_back(arma::norm(arma::sqrt(solver.getSolver().Om())* (solver.Psi()* Vo - fom_snapshots.col(snapcounter))));
		_ = solver.getSolver().Om() * fom_snapshots.col(snapcounter);
		__ = solver.Psi().t() * _;
		_ = solver.Psi() * __;
		__ = arma::speye(numm, numm) * fom_snapshots.col(snapcounter);
		idealnorms.push_back(arma::norm(arma::sqrt(solver.getSolver().Om())* (__ - _)));
		++snapcounter;

#endif

		for (int k = 0; k < m_tableau.s; ++k) {
			Vo += dt * m_tableau.b[k] * (-solver.Nr(getStage(stagesNext, k + 1)) + nu * solver.Dr() * getStage(stagesNext, k + 1));
		}

		stagesNext = arma::repmat(arma::Mat<double>(Vo), m_tableau.s, 1).as_col();

		t = t + dt;

		if (abs(finalT - t) < (0.01 * dt)) {
			std::cout.precision(17);
			std::cout << t << std::endl;
#ifdef CALCULATE_ENERGY
			arma::Col<double>(kineticEnergy).save("GL4_rom_kinetic_energy.txt", arma::raw_ascii);
			//momentum.save("GL4_rom_momentum.txt", arma::raw_ascii);
#endif
#ifdef CALCULATE_ROM_ERROR

			arma::Col<double>(errornorms).save("GL4_error_norm.txt", arma::raw_ascii);
			arma::Col<double>(idealnorms).save("GL4_ideal_norm.txt", arma::raw_ascii);

#endif

			return Vo;
		}

		if (t > finalT) {
			std::cout.precision(17);
			std::cout << t << std::endl;
#ifdef CALCULATE_ENERGY
			arma::Col<double>(kineticEnergy).save("GL4_rom_kinetic_energy.txt", arma::raw_ascii);
			//momentum.save("GL4_rom_momentum.txt", arma::raw_ascii);
#endif
#ifdef CALCULATE_ROM_ERROR

			arma::Col<double>(errornorms).save("GL4_error_norm.txt", arma::raw_ascii);
			arma::Col<double>(idealnorms).save("GL4_ideal_norm.txt", arma::raw_ascii);

#endif
			return prevVo;
		}
		//}

#ifdef PRINT_TIME
		std::cout << "time: " << t << std::endl;
#endif
		

#ifdef CALCULATE_ENERGY
		kineticEnergy.push_back(0.5 * arma::as_scalar(Vo.t() * Vo));
		//momentum = arma::join_rows(momentum, momMat * Vo);
#endif

	}

#ifdef CALCULATE_ENERGY
	arma::Col<double>(kineticEnergy).save("GL4_rom_kinetic_energy.txt", arma::raw_ascii);
	//momentum.save("GL4_rom_momentum.txt", arma::raw_ascii);
#endif

	//arma::Mat<double>(S).save("matrix.txt", arma::raw_ascii);

	return Vo;
}

template arma::Col<double> ImplicitRungeKutta_ROM<false>::integrate(double finalT, double dt, const arma::Col<double>& initialVel, const arma::Col<double>& initialP, const ROM_Solver& solver, double collectTime);
template arma::Col<double> ImplicitRungeKutta_ROM<true>::integrate(double finalT, double dt, const arma::Col<double>& initialVel, const arma::Col<double>& initialP, const ROM_Solver& solver, double collectTime);




template<bool COLLECT_DATA>
arma::Col<double> RelaxationRungeKutta_ROM<COLLECT_DATA>::integrate(double finalT, double dt, const arma::Col<double>& initialA, const arma::Col<double>& initialP, const ROM_Solver& solver, double collectTime) {

#ifdef CALCULATE_ENERGY
	std::vector<double> kineticEnergy;
#endif

	//RRK relaxation parameter
	double gamma = 0.0;
	double gammaDenom = 0.0;
	double gammaNom = 0.0;

	arma::Mat<double> fifj(m_tableau.s, m_tableau.s);

	std::vector<arma::Col<double>> as;
	std::vector<arma::Col<double>> Fs;

	arma::Col<double> ao = initialA;
	arma::Col<double> a;

	double nu = solver.nu();
	double t = 0.0;

	while (t < finalT) {

		as.push_back(ao);

		for (int i = 0; i < m_tableau.s; ++i) {

			a = ao;

			Fs.push_back((-solver.Nr(as[i]) + nu * solver.Dr() * as[i]));

			for (int j = 0; j < (i + 1); ++j) {

				if (i < (m_tableau.s - 1)) {
					a += dt * m_tableau.A[i + 1][j] * Fs[j];
				}
				else {

					//code is ran 3 times
					for (arma::uword k = 0; k < m_tableau.s; ++k) {
						for (arma::uword l = 0; l < m_tableau.s; ++l) {

							fifj(k, l) = arma::as_scalar(Fs[k].t() * Fs[l]);

						}
					}

					gammaDenom = 0.0;
					gammaNom = 0.0;

					for (int k = 0; k < m_tableau.s; ++k) {
						for (int l = 0; l < m_tableau.s; ++l) {

							gammaNom += m_tableau.b[k] * m_tableau.A[k][l] * fifj(k, l);
							gammaDenom += m_tableau.b[k] * m_tableau.b[l] * fifj(k, l);

						}
					}

					gamma = (abs(gammaDenom) > 1e-17) ? (2.0 * gammaNom) / gammaDenom : 1.0;

					//std::cout << "gamma: " << (2.0 * gammaNom) / gammaDenom << ", denominator: " << gammaDenom << ", numerator: " << gammaNom << std::endl;

					a += gamma * dt * m_tableau.b[j] * Fs[j];
				}

			}

			as.push_back(a);
		}

		if constexpr (Base_ROM_Integrator<COLLECT_DATA>::m_collector.COLLECT_DATA) {
			if (t <= collectTime) {
				Base_ROM_Integrator<COLLECT_DATA>::m_collector.addColumn(ao);
				Base_ROM_Integrator<COLLECT_DATA>::m_collector.addOperatorColumn(solver.Nr(ao));
			}
		}

		ao = as.back();

#ifdef CALCULATE_ENERGY
		kineticEnergy.push_back(0.5 * arma::as_scalar(ao.t() * ao));
#endif

		as.clear();
		Fs.clear();

		t = t + gamma * dt;

#ifdef PRINT_TIME
		std::cout << "time: " << t << std::endl;
#endif
	}

#ifdef CALCULATE_ENERGY
	arma::Col<double>(kineticEnergy).save("rom_kinetic_energy.txt", arma::raw_ascii);
#endif

	return ao;
}

template arma::Col<double> RelaxationRungeKutta_ROM<false>::integrate(double finalT, double dt, const arma::Col<double>& initialA, const arma::Col<double>& initialP, const ROM_Solver& solver, double collectTime);
template arma::Col<double> RelaxationRungeKutta_ROM<true>::integrate(double finalT, double dt, const arma::Col<double>& initialA, const arma::Col<double>& initialP, const ROM_Solver& solver, double collectTime);


template<bool COLLECT_DATA>
const dataCollector<COLLECT_DATA>& Base_ROM_Integrator<COLLECT_DATA>::getDataCollector() const {
	return m_collector;
}

template const dataCollector<true>& Base_ROM_Integrator<true>::getDataCollector() const;
template const dataCollector<false>& Base_ROM_Integrator<false>::getDataCollector() const;
