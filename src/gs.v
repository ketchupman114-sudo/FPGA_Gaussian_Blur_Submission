// ============================================================
// Gaussian Blur (3x3) for 320x240 RGB565 image stored in a HEX file
// - Input HEX:  76800 lines, each 16-bit RGB565 (4 hex digits)
// - Output HEX: 76800 lines, each 16-bit RGB565 (4 hex digits)
// Notes:
//  - Uses $readmemh/$writememh (simulation-friendly)
//  - Fixed to avoid 'xxxx' in output by always writing every output address
//  - Adds 1-cycle alignment regs so window/address are consistent in simulation
// ============================================================

`timescale 1ns/1ps

// ------------------------------------------------------------
// DUT: Reads IN_HEX into frame_buffer, produces blur_buffer
// ------------------------------------------------------------
module gaussian_blur_rgb565_320x240 #(
    // More ModelSim-friendly than "parameter string"
    parameter IN_HEX = "output.hex"
)(
    input  wire clk,
    input  wire rst,
    input  wire start,
    output reg  done
);
    localparam integer W = 320;
    localparam integer H = 240;
    localparam integer N = W*H;

    // ----------------------------
    // Input frame buffer (RGB565)
    // ----------------------------
    reg [15:0] frame_buffer [0:N-1];
    initial begin
        $readmemh(IN_HEX, frame_buffer);
    end

    // ----------------------------
    // Output blurred buffer (RGB565)
    // ----------------------------
    reg [15:0] blur_buffer [0:N-1];

    // ----------------------------
    // Two line buffers (previous 2 rows)
    // ----------------------------
    reg [15:0] line1 [0:W-1];  // row y-1
    reg [15:0] line2 [0:W-1];  // row y-2

    // Scan counters
    reg [8:0]  x;      // 0..319
    reg [7:0]  y;      // 0..239
    reg [16:0] addr;   // 0..76799
    reg        running;

    // 1-cycle delayed counters/address to align with pipeline
    reg [8:0]  x_d;
    reg [7:0]  y_d;
    reg [16:0] addr_d;

    // Current and tapped pixels
    reg [15:0] p_in;
    reg [15:0] p_mid;
    reg [15:0] p_top;

    // 1-cycle delayed input pixel used for guaranteed-defined write
    reg [15:0] p_in_d;

    // 3x3 window shift regs
    reg [15:0] r0_0, r0_1, r0_2;
    reg [15:0] r1_0, r1_1, r1_2;
    reg [15:0] r2_0, r2_1, r2_2;

    // Channel extract helpers
    function automatic [4:0] R5(input [15:0] p); begin R5 = p[15:11]; end endfunction
    function automatic [5:0] G6(input [15:0] p); begin G6 = p[10:5];  end endfunction
    function automatic [4:0] B5(input [15:0] p); begin B5 = p[4:0];   end endfunction

    // Gaussian weights:
    // 1 2 1
    // 2 4 2   all / 16
    // 1 2 1
    wire [9:0] sumR =
        (R5(r0_0)) + (R5(r0_1)<<1) + (R5(r0_2)) +
        (R5(r1_0)<<1) + (R5(r1_1)<<2) + (R5(r1_2)<<1) +
        (R5(r2_0)) + (R5(r2_1)<<1) + (R5(r2_2));

    wire [9:0] sumG =
        (G6(r0_0)) + (G6(r0_1)<<1) + (G6(r0_2)) +
        (G6(r1_0)<<1) + (G6(r1_1)<<2) + (G6(r1_2)<<1) +
        (G6(r2_0)) + (G6(r2_1)<<1) + (G6(r2_2));

    wire [9:0] sumB =
        (B5(r0_0)) + (B5(r0_1)<<1) + (B5(r0_2)) +
        (B5(r1_0)<<1) + (B5(r1_1)<<2) + (B5(r1_2)<<1) +
        (B5(r2_0)) + (B5(r2_1)<<1) + (B5(r2_2));

    wire [4:0] blurR = sumR[9:4]; // /16
    wire [5:0] blurG = sumG[9:4]; // /16
    wire [4:0] blurB = sumB[9:4]; // /16

    wire [15:0] blurred_pixel = {blurR, blurG, blurB};

    // Window valid after x_d>=2,y_d>=2; output aligns to center (x_d-1,y_d-1)
    wire [8:0]  out_x    = x_d - 1;
    wire [7:0]  out_y    = y_d - 1;
    wire [16:0] out_addr = out_y * W + out_x;

    integer i;

    always @(posedge clk) begin
        if (rst) begin
            x <= 0; y <= 0; addr <= 0;
            x_d <= 0; y_d <= 0; addr_d <= 0;
            running <= 0;
            done <= 0;

            p_in <= 0; p_mid <= 0; p_top <= 0;
            p_in_d <= 0;

            r0_0<=0; r0_1<=0; r0_2<=0;
            r1_0<=0; r1_1<=0; r1_2<=0;
            r2_0<=0; r2_1<=0; r2_2<=0;

            for (i=0; i<W; i=i+1) begin
                line1[i] <= 0;
                line2[i] <= 0;
            end
        end else begin
            done <= 0;

            if (start && !running) begin
                running <= 1;
                x <= 0; y <= 0; addr <= 0;
                x_d <= 0; y_d <= 0; addr_d <= 0;
            end

            if (running) begin
                // Read pixel at (x,y)
                p_in  <= frame_buffer[addr];

                // Taps from previous rows at same x
                p_mid <= line1[x];
                p_top <= line2[x];

                // Delay counters/address for aligned writes
                x_d    <= x;
                y_d    <= y;
                addr_d <= addr;

                // Delay pixel to ensure defined copy-out each address
                p_in_d <= p_in;

                // Shift 3x3 window, insert newest column on the right
                r0_0 <= r0_1;  r0_1 <= r0_2;  r0_2 <= p_top;
                r1_0 <= r1_1;  r1_1 <= r1_2;  r1_2 <= p_mid;
                r2_0 <= r2_1;  r2_1 <= r2_2;  r2_2 <= p_in;

                // Update line buffers
                line2[x] <= line1[x];
                line1[x] <= p_in;

                // FIX: Always write a defined value to every output address.
                // This prevents 'xxxx' in the written hex.
                blur_buffer[addr_d] <= p_in_d;

                // Overwrite interior pixels when window is valid
                if (x_d >= 2 && y_d >= 2) begin
                    blur_buffer[out_addr] <= blurred_pixel;
                end

                // Advance scan
                if (addr == N-1) begin
                    running <= 0;
                    done <= 1;
                end else begin
                    addr <= addr + 1;

                    if (x == W-1) begin
                        x <= 0;
                        y <= y + 1;
                    end else begin
                        x <= x + 1;
                    end
                end
            end
        end
    end

endmodule


// ------------------------------------------------------------
// Testbench: runs the blur once and writes blurred.hex
// ------------------------------------------------------------
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
