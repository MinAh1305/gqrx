/* -*- c++ -*- */
/*
 * Gqrx SDR: Software defined radio receiver powered by GNU Radio and Qt
 *           http://gqrx.dk/
 *
 * Copyright 2011 Alexandru Csete OZ9AEC.
 *
 * Gqrx is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * Gqrx is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Gqrx; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */
#include <QMessageBox>
#include <QFileDialog>
#include <cmath>
#include <gnuradio/io_signature.h>
#include <gnuradio/filter/firdes.h>
#include <gnuradio/digital/mpsk_receiver_cc.h>
#include <gnuradio/blocks/complex_to_real.h>
#include <iostream>
#include <stdio.h>
#include <stdarg.h>
#include "dsp/rx_rds.h"

static const int MIN_IN = 1;  /* Mininum number of input streams. */
static const int MAX_IN = 1;  /* Maximum number of input streams. */
static const int MIN_OUT = 1; /* Minimum number of output streams. */
static const int MAX_OUT = 1; /* Maximum number of output streams. */

//static const double baseband_rate = 250000;
//static const double baseband_rate = 250000;
static const double baseband_rate = 250000;

/*
 * Create a new instance of rx_rds and return
 * a boost shared_ptr. This is effectively the public constructor.
 */
rx_rds_sptr make_rx_rds(double sample_rate, double midle_rate, double low, double high, double trans_width, float quad_rate, float audio_rate)
{
    return gnuradio::get_initial_sptr(new rx_rds(sample_rate, midle_rate, low, high, trans_width, quad_rate, audio_rate));
}

rx_rds_store_sptr make_rx_rds_store()
{
    return gnuradio::get_initial_sptr(new rx_rds_store());
}

rx_rds::rx_rds(double sample_rate, double midle_rate, double low, double high, double trans_width, float quad_rate, float audio_rate)
    : gr::hier_block2 ("rx_rds",
                      gr::io_signature::make (MIN_IN, MAX_IN, sizeof (float)),
                      gr::io_signature::make (MIN_OUT, MAX_OUT, sizeof (char))),
      d_sample_rate(sample_rate),
      d_midle_rate(midle_rate),
      d_low(low),
      d_high(high),
      d_trans_width(trans_width),
      d_quad_rate(quad_rate),
      d_audio_rate(audio_rate)
{
    if (low < -0.95*sample_rate/2.0)
        d_low = -0.95*sample_rate/2.0;
    if (high > 0.95*sample_rate/2.0)
        d_high = 0.95*sample_rate/2.0;

    audio_decim = sample_rate/midle_rate;
    float baseband_rate;
    baseband_rate=sample_rate/audio_decim;


    printf("XXX d_sample_rate %f d_low: %f d_high: %f d_trans_width: %f AUDIO_DECIM: %f quad_rate: %f audio_rate: %f baseband_rate: %f\n",d_sample_rate, d_low, d_high, d_trans_width, audio_decim, d_quad_rate, d_audio_rate, baseband_rate);


    d_taps2 = gr::filter::firdes::low_pass(1.0, d_sample_rate, 57000, 4800);

    f_fxff = gr::filter::freq_xlating_fir_filter_fcf::make(1.0, d_taps2, 57000, d_sample_rate);

    f_rrcf = gr::filter::firdes::root_raised_cosine(1, sample_rate/1, 2375, 1, 100);
    d_bpf2 = gr::filter::fir_filter_ccf::make(1, f_rrcf);

    d_mpsk = gr::digital::mpsk_receiver_cc::make(2, 0, 1*M_PI/100.0, -0.06, 0.06, 0.5, 0.05, sample_rate/1/2375.0, 0.001, 0.005);


    b_ctr = gr::blocks::complex_to_real::make(1);

    d_bs = gr::digital::binary_slicer_fb::make();

    b_koin = gr::blocks::keep_one_in_n::make(sizeof(unsigned char), 2);

    d_ddbb = gr::digital::diff_decoder_bb::make(2);

    rds_decoder = gr::rds::decoder::make(0, 0);
    rds_parser = gr::rds::parser::make(1, 0);

    udp_sink1 = gr::blocks::udp_sink::make(4, "127.0.0.1", 4441);
    udp_sink2 = gr::blocks::udp_sink::make(8, "127.0.0.1", 4442);
    udp_sink3 = gr::blocks::udp_sink::make(8, "127.0.0.1", 4443);
    udp_sink4 = gr::blocks::udp_sink::make(8, "127.0.0.1", 4444);

    dbg = gr::blocks::message_debug::make();

    /* connect filter */
    connect(self(), 0, f_fxff, 0);
    connect(self(), 0, udp_sink1, 0);
    connect(f_fxff, 0, d_bpf2, 0);
    connect(f_fxff, 0, udp_sink2, 0);
    connect(d_bpf2, 0, d_mpsk, 0);
    connect(d_bpf2, 0, udp_sink3, 0);
    connect(d_mpsk, 0, b_ctr, 0);
    connect(d_mpsk, 0, udp_sink4, 0);
    connect(b_ctr, 0, d_bs, 0);
    connect(d_bs, 0, b_koin, 0);
    connect(b_koin, 0, d_ddbb, 0);
    connect(d_ddbb, 0, self(), 0);
}

rx_rds::~rx_rds ()
{

}


void rx_rds::set_param(double low, double high, double trans_width)
{
    d_trans_width = trans_width;
    d_low         = low;
    d_high        = high;

    printf("XXX2 d_sample_rate %f d_low: %f d_high: %f d_trans_width: %f\n",d_sample_rate, d_low, d_high, d_trans_width);

    if (d_low < -0.95*d_sample_rate/2.0)
        d_low = -0.95*d_sample_rate/2.0;
    if (d_high > 0.95*d_sample_rate/2.0)
        d_high = 0.95*d_sample_rate/2.0;

    /* generate new taps */
    //d_taps = gr::filter::firdes::complex_band_pass(1.0, d_sample_rate, d_low, d_high, d_trans_width);

    //d_taps2 = gr::filter::firdes::low_pass(2500.0, baseband_rate, 2.4000, 2000, gr::filter::firdes::WIN_HAMMING);
#ifndef QT_NO_DEBUG_OUTPUT
    std::cout << "RDS Genrating taps for new filter LO:" << d_low <<
                 " HI:" << d_high << " TW:" << d_trans_width << std::endl;
    std::cout << "RDS Required number of taps: " << d_taps.size() << std::endl;
#endif
    //f_fxff->set_taps(d_taps2);
    //d_bpf->set_taps(d_taps);
}

rx_rds_store::rx_rds_store() : gr::block ("rx_rds_store",
                                gr::io_signature::make (0, 0, 0),
                                gr::io_signature::make (0, 0, 0))
{
        message_port_register_in(pmt::mp("store"));
        set_msg_handler(pmt::mp("store"), boost::bind(&rx_rds_store::store, this, _1));
        d_messages.set_capacity(100);
}

rx_rds_store::~rx_rds_store ()
{

}

void rx_rds_store::store(pmt::pmt_t msg)
{

    //t = pmt.to_long(pmt.tuple_ref(msg, 0))
   // m = pmt.symbol_to_string(pmt.tuple_ref(msg, 1))

    boost::mutex::scoped_lock lock(d_mutex);

    //std::string text=pmt::symbol_to_string(pmt::tuple_ref(msg,1));
    //std::cout << "msg: " << text << std::endl;


    //std::cout << "d_messages.size(): " << d_messages.size() << std::endl;

    d_messages.push_back(msg);

}

void rx_rds_store::get_message(std::string &out, int &type)
{
    boost::mutex::scoped_lock lock(d_mutex);

    if (d_messages.size()>0) {
        //std::string *buff = d_messages.linearize();

        //memcpy(out, buff, sizeof(std::string)*num);
        //type=

        //d_messages.clear();
        pmt::pmt_t msg=d_messages.front();
        type=pmt::to_long(pmt::tuple_ref(msg,0));
        out=pmt::symbol_to_string(pmt::tuple_ref(msg,1));
        d_messages.pop_front();
    } else {
        type=-1;
    }
}
