`timescale 1ns/1ps
module tb_gaussian_blur;

    reg clk = 0;
    reg rst = 1;
    reg start = 0;
    wire done;

    // ModelSim-friendly: avoid "localparam string"
    localparam INFILE  = "output.hex";
    localparam OUTFILE = "blurred.hex";

    gaussian_blur_rgb565_320x240 #(
        .IN_HEX(INFILE)
    ) dut (
        .clk(clk),
        .rst(rst),
        .start(start),
        .done(done)
    );

    always #5 clk = ~clk; // 100 MHz

    initial begin
        // Reset
        #30 rst = 0;

        // Start pulse
        #20 start = 1;
        #10 start = 0;

        // Wait for completion
        wait(done);

        // Write the blurred image as a hex file (same format: 16-bit per line)
        $writememh(OUTFILE, dut.blur_buffer);

        $display("Blur complete! Wrote %s", OUTFILE);
        #20 $finish;
    end

endmodule
