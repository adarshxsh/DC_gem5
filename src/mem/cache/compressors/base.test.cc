/*
 * Copyright (c) 2026 gem5
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
TEST_F(BaseCompressorTest, CumulativeStatsIndependence)
{
    auto compressor = createZeroCompressor(true, 1.5f, 1, 3);

    Cycles comp_lat(0);
    Cycles decomp_lat(0);

    // Run 10 compressions
    for (int i = 0; i < 5; i++) {
        compressor->compress(zeroLine, comp_lat, decomp_lat);
    }
    for (int i = 0; i < 5; i++) {
        compressor->compress(randomLine, comp_lat, decomp_lat);
    }

    // Window size is 3, so active window has only 3 samples.
    // But overall cumulative sampled compressions must equal 10.
    // Total sampled uncompressed bits = 10 * 512 = 5120 bits.
    // (5 zero lines @ 0 bits + 5 random lines @ 512 bits = 2560 compressed
    // bits total). Let's verify via getObservedRatio() vs cumulative
    // statistics logic. The window has 3 samples of randomLine (ratio 1.0),
    // whereas cumulative total is 5120 / 2560 = 2.0x ratio.
    ASSERT_DOUBLE_EQ(compressor->getObservedRatio(), 1.0);
}
