// src/lib/webgpu/debug.ts

import type { GpuBuffers } from './types';

export async function downloadPointCloudData(device: GPUDevice, buffers: GpuBuffers) {
    console.log("Starting data download...");

    const { counter_pass1, point_counter, point_cloud_buffer } = buffers;

    // Create readable buffers
    const readableCounter1 = device.createBuffer({ size: 4, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
    const readablePointCounter = device.createBuffer({ size: 4, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
    const readablePointCloud = device.createBuffer({ size: point_cloud_buffer.size, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });

    // Encode copy commands
    const commandEncoder = device.createCommandEncoder();
    commandEncoder.copyBufferToBuffer(counter_pass1, 0, readableCounter1, 0, 4);
    commandEncoder.copyBufferToBuffer(point_counter, 0, readablePointCounter, 0, 4);
    commandEncoder.copyBufferToBuffer(point_cloud_buffer, 0, readablePointCloud, 0, point_cloud_buffer.size);
    device.queue.submit([commandEncoder.finish()]);

    // Wait for mapping
    await Promise.all([
        readableCounter1.mapAsync(GPUMapMode.READ),
        readablePointCounter.mapAsync(GPUMapMode.READ),
        readablePointCloud.mapAsync(GPUMapMode.READ),
    ]);

    // Process and download data
    const counter1Array = new Uint32Array(readableCounter1.getMappedRange());
    const pointCounterArray = new Uint32Array(readablePointCounter.getMappedRange());
    const pointCloudArray = new Float32Array(readablePointCloud.getMappedRange());

    let fileContent = `// Pass 1 Found Cells: ${counter1Array[0]}\n`;
    fileContent += `// Pass 3 Generated Points: ${pointCounterArray[0]}\n\n`;
    fileContent += `// --- Point Cloud Data (X, Y) --- \n`;
    for (let i = 0; i < pointCounterArray[0]; i++) {
        fileContent += `${pointCloudArray[i * 2]}, ${pointCloudArray[i * 2 + 1]}\n`;
    }

    const blob = new Blob([fileContent], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'point_data.txt';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
    console.log("Data download complete.");

    // Unmap buffers
    readableCounter1.unmap();
    readablePointCounter.unmap();
    readablePointCloud.unmap();
}