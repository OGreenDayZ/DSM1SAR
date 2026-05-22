//    if ((time_dsm = test_matrix_multiplication(nb_compute_nodes, node_id, 1024, config))
//    <
//         0)
//         goto cleanup;
//     else
//         test_ok++;
//     dsm_barrier();
//     if ((time_local = test_matrix_multiplication_local(nb_compute_nodes, 1024,
//     node_id)) <
//         0)
//         goto cleanup;
//     test++;
//     LOG_SUBSECTION(BOLD BRIGHT_BLUE
//                    "Ratio de comparaison Local vs DSM - Matrice 1024x1024 - %d%%"
//                    RESET, (int)((time_dsm - time_local) / time_local) * 100);
//     dsm_barrier();

//     if ((time_dsm = test_matrix_multiplication(nb_compute_nodes, node_id, 2048,
//     config)) <
//         0)
//         goto cleanup;
//     else
//         test_ok++;
//     dsm_barrier();
//     if ((time_local = test_matrix_multiplication_local(nb_compute_nodes, 2048,
//     node_id)) <
//         0)
//         goto cleanup;
//     test++;
//     LOG_SUBSECTION(BOLD BRIGHT_BLUE
//                    "Ratio de comparaison Local vs DSM - Matrice 2048x2048 - %d%%"
//                    RESET, (int)((time_dsm - time_local) / time_local) * 100);
//     dsm_barrier();