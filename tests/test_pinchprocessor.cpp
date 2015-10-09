/*
  Copyright 2015 Statoil ASA.

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>

#if HAVE_DYNAMIC_BOOST_TEST
#define BOOST_TEST_DYN_LINK
#endif

#define NVERBOSE // Suppress own messages when throw()in 

#define BOOST_TEST_MODULE PinchProcessorTest
#include <boost/test/unit_test.hpp>
#include <vector>
#include <opm/core/grid.h>
#include <opm/core/grid/GridManager.hpp>
#include <opm/parser/eclipse/Parser/Parser.hpp>
#include <opm/parser/eclipse/Parser/ParseMode.hpp>
#include <opm/parser/eclipse/Deck/Deck.hpp>
#include <opm/parser/eclipse/Deck/DeckKeyword.hpp>
#include <opm/parser/eclipse/EclipseState/EclipseState.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/EclipseGrid.hpp>
#include <opm/core/pressure/tpfa/TransTpfa.hpp>
#include <opm/core/grid/PinchProcessor.hpp>
#include <opm/core/props/rock/RockFromDeck.hpp>


using namespace Opm;
BOOST_AUTO_TEST_CASE(Processing)
{
    const std::string filename="../tests/testPinch1.DATA";
    Opm::ParserPtr parser(new Opm::Parser());
    Opm::ParseMode parseMode({{ ParseMode::PARSE_RANDOM_SLASH , InputError::IGNORE }});
    Opm::DeckConstPtr deck = parser->parseFile(filename, parseMode);
    std::shared_ptr<EclipseState> eclstate (new Opm::EclipseState(deck, parseMode));
    std::vector<double> porv = eclstate->getDoubleGridProperty("PORV")->getData();
    EclipseGridConstPtr eclgrid = eclstate->getEclipseGrid();
    Opm::GridManager gridM(eclgrid);
    typedef UnstructuredGrid Grid;
    const Grid& grid = *(gridM.c_grid());
    const int* global_cell = Opm::UgGridHelpers::globalCell(grid);
    const int* cart_dims = Opm::UgGridHelpers::cartDims(grid);
    const int nc = Opm::UgGridHelpers::numCells(grid);
    Opm::RockFromDeck rock;
    rock.init(eclstate, nc, global_cell, cart_dims);

    const double minpv = eclgrid->getMinpvValue();
    BOOST_CHECK_EQUAL(minpv, 0.001);

    const double thickness = eclgrid->getPinchThresholdThickness();
    BOOST_CHECK_EQUAL(thickness, 0.001);

    auto transMode = eclgrid->getPinchOption();
    BOOST_CHECK_EQUAL(transMode, PinchMode::ModeEnum::TOPBOT);

    auto multzMode = eclgrid->getMultzOption();
    BOOST_CHECK_EQUAL(multzMode, PinchMode::ModeEnum::TOP);

    PinchProcessor<Grid> pinch(minpv, thickness, transMode, multzMode);
    std::vector<int> actnum;
    eclgrid->exportACTNUM(actnum);

    if (actnum.empty() && (nc == eclgrid->getCartesianSize())) {
        actnum.assign(nc, 1);
    }
    std::vector<double> htrans(Opm::UgGridHelpers::numCellFaces(grid));
    Grid* ug = const_cast<Grid*>(& grid);
    tpfa_htrans_compute(ug, rock.permeability(), htrans.data());
    auto transMult = eclstate->getTransMult();
    std::vector<double> multz(nc, 0.0);
    std::vector<double> dz(porv.size(), 0.0);
    for (int i = 0; i < nc; ++i) {
        multz[i] = transMult->getMultiplier(global_cell[i], Opm::FaceDir::ZPlus);
    }
    Opm::NNC nnc(deck, eclgrid);
    pinch.process(grid, htrans, actnum, multz, porv, dz, nnc);
    auto nnc1 = nnc.nnc1();
    auto nnc2 = nnc.nnc2();
    auto trans = nnc.trans();

    BOOST_CHECK(nnc.hasNNC());
    BOOST_CHECK_EQUAL(nnc.numNNC(), 1);
   
    auto nnc1_index = 1 + cart_dims[0] * (0 + cart_dims[1] * 0);
    auto nnc2_index = 1 + cart_dims[0] * (0 + cart_dims[1] * 3);
    BOOST_CHECK_EQUAL(nnc1[0], nnc1_index);
    BOOST_CHECK_EQUAL(nnc2[0], nnc2_index);
    double factor = Opm::prefix::centi*Opm::unit::Poise 
        * Opm::unit::cubic(Opm::unit::meter)
        / Opm::unit::day
        / Opm::unit::barsa;
    for (auto& tran : trans) {
        tran = unit::convert::to(tran, factor);
    }
    BOOST_CHECK(std::fabs(trans[0]-4.26350022) < 1e-3);
}
