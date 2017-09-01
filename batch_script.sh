#!/bin/bash
#SBATCH -A damodar
#SBATCH --exclusive
#SBATCH --nodes=8
#SBATCH -J randomInc_job
#SBATCH --partition=batch-storage
#SBATCH --time=2-00:00:00

module load mvapich2-2.1
python run_processPerNode.py slurm-${SLURM_JOB_ID}.log 
