#include <iostream>
#include <vector>
#include <map>
#include <math.h>
#include <algorithm>
#include <mpi.h>
#include <omp.h>
#include <chrono>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

using namespace std;

typedef struct {
	int label;
	float x, y;
} Point;

vector<pair<Point, float>> oddevenSort(vector<pair<Point, float>> a, int N) {
	// последовательная реализация - O(n^2)
	// параллельная - O((n^2)/p)
	for (int i = 0; i < N; i++) {
		pair<Point, float> v;
		#pragma omp parallel for private(v) num_threads(4)
		for (int j = i % 2 ? 0 : 1; j < N; j += 2)
			if (j < N - 1)
				if (a[j].second > a[j + 1].second) {
					v = a[j];
					a[j] = a[j + 1];
					a[j + 1] = v;
				}
	}
	return a;
}

vector<Point> get_train_set(int N) {
	vector<Point> dataset;
	Point p;
	float LO = 0.0, HI = 50.0;
	for (int i = 0; i < N; i++) {
		p.x = LO + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (HI - LO)));
		p.y = LO + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (HI - LO)));
		p.label = 0 + (rand() % static_cast<int>(2));
		dataset.push_back(p);
	}
	return dataset;
}

vector<Point> get_test_set(int N) {
	vector<Point> dataset;
	Point p;
	float LO = 0.0, HI = 50.0;
	for (int i = 0; i < N; i++) {
		p.x = LO + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (HI - LO)));
		p.y = LO + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (HI - LO)));
		dataset.push_back(p);
	}
	return dataset;
}

void print_dataset(vector<Point> dataset) {
	for (auto p : dataset) {
		printf("(%f, %f) - %d\n", p.x, p.y, p.label);
	}
	return;
}

float distance(Point p1, Point p2) {
	return sqrt(pow(p1.x - p2.x, 2) + pow(p1.y - p2.y, 2));
}

bool cmp(pair<Point, float> &a, pair<Point, float> &b) {
	return a.second < b.second;
}

vector<Point> knn_classify_dataset(vector<Point> train, vector<Point> test, int k) {
	vector<Point> result;
	for (Point test_point : test) {
		vector<pair<Point, float>> distances;
		#pragma omp parallel num_threads(4)
		{
			#pragma omp for schedule(static)
			for (int i = 0; i < train.size(); i++) {
				pair<Point, float> p(train[i], distance(train[i], test_point));
				#pragma omp critical
				{
					distances.push_back(p);
				}
			}
		}

		//sort(distances.begin(), distances.end(), cmp);
		distances = oddevenSort(distances, distances.size());

		float s = 0;
		int counter = 0;
		for (vector<pair<Point, float>>::iterator it = distances.begin(); it != distances.end(); ++it) {
			s += it->first.label;
			counter++;
			if (counter >= k)
				break;
		}

		Point p;
		p.x = test_point.x;
		p.y = test_point.y;
		p.label = (int)round(s / k);
		result.push_back(p);
	}
	return result;
}

int main(int argc, char* argv[]) {
	MPI_Init(&argc, &argv);
	int rank, size;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Comm_size(MPI_COMM_WORLD, &size);

	chrono::time_point<chrono::system_clock> start, end;
	if (rank == 0)
		start = chrono::system_clock::now();

	MPI_Datatype MPI_POINT;
	int blocklengths[3] = {1, 1, 1};
	MPI_Datatype types[3] = {MPI_INT, MPI_FLOAT, MPI_FLOAT};
	MPI_Aint displacements[3];
	MPI_Aint intex, floatex;
	MPI_Type_extent(MPI_INT, &intex);
	MPI_Type_extent(MPI_FLOAT, &floatex);
	displacements[0] = (MPI_Aint)0;
	displacements[1] = intex;
	displacements[2] = intex + floatex;
	MPI_Type_struct(3, blocklengths, displacements, types, &MPI_POINT);
	MPI_Type_commit(&MPI_POINT);

	int N_train = 400, N_test = 80;
	vector<Point> X_train = vector<Point>(N_train);
	vector<Point> X_test = vector<Point>(N_test);
	if (rank == 0) {
		X_train = get_train_set(N_train);
		X_test = get_test_set(N_test);
	}
	MPI_Bcast(&X_train[0], N_train, MPI_POINT, 0, MPI_COMM_WORLD);
	MPI_Barrier(MPI_COMM_WORLD);

	vector<Point> X_test_part = vector<Point>(N_test / size);
	MPI_Scatter(&X_test[0], N_test / size, MPI_POINT, &X_test_part[0], N_test / size, MPI_POINT, 0, MPI_COMM_WORLD);
	MPI_Barrier(MPI_COMM_WORLD);

	X_test_part = knn_classify_dataset(X_train, X_test_part, 5);
	MPI_Barrier(MPI_COMM_WORLD);

	MPI_Gather(&X_test_part[0], N_test / size, MPI_POINT, &X_test[0], N_test / size, MPI_POINT, 0, MPI_COMM_WORLD);
	MPI_Barrier(MPI_COMM_WORLD);

	if (rank == 0) {
		print_dataset(X_test);
	}

	if (rank == 0) {
		end = chrono::system_clock::now();
		int elapsed_seconds = chrono::duration_cast<chrono::milliseconds>(end - start).count();
		printf("Elapsed time: %d milliseconds\n", elapsed_seconds);
	}
	
	MPI_Finalize();
	return 0;
}